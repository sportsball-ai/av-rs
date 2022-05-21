use super::EncodeError;
use crate::muxer::{Muxer, Packet, Stream, StreamConfig};
use alloc::collections::vec_deque::VecDeque;
use alloc::vec::Vec;
use core::time::Duration;
use core2::io::Write;

const TS_33BIT_MASK: u64 = 0x1FFFFFFFF;

pub struct InterleavingMuxer<W: Write> {
    inner: Muxer<W>,
    max_buffer_duration_90khz: u64,
    largest_ts_in_buffer: u64,
    streams: Vec<InterleavingStream>,
    last_fixed_timestamp: u64,
}

pub struct InterleavingStream {
    inner: Stream,
    buffered_packets: VecDeque<(usize, u64, Option<Packet<'static>>)>,
    last_written_ts: u64,
}

impl<W: Write> InterleavingMuxer<W> {
    pub fn new(w: W, max_buffer_duration: Duration) -> Self {
        let muxer = Muxer::new(w);
        Self {
            inner: muxer,
            max_buffer_duration_90khz: (max_buffer_duration.as_millis() * 90) as u64,
            largest_ts_in_buffer: 0,
            streams: vec![],
            last_fixed_timestamp: 0,
        }
    }

    pub fn add_stream(&mut self, config: StreamConfig) {
        let inner = self.inner.new_stream(config);
        let interleaving_stream = InterleavingStream {
            inner,
            buffered_packets: VecDeque::new(),
            last_written_ts: 0,
        };
        self.streams.push(interleaving_stream);
    }

    /// This function returns a timestamp with high bits added in order to keep it as close as possible to `last_fixed_timestamp`.
    /// To correctly track overflows, `last_fixed_timestamp` should be the timestamp that was previously returned by this function.
    fn fixed_timestamp(&mut self, ts: u64) -> u64 {
        self.last_fixed_timestamp = if (self.last_fixed_timestamp & TS_33BIT_MASK) > 0x180000000 && ts < 0x80000000 {
            // It looks like ts rolled over. This must be the first timestamp of a new epoch.
            ts | ((self.last_fixed_timestamp >> 33) + 1) << 33
        } else if (self.last_fixed_timestamp & TS_33BIT_MASK) < 0x80000000 && ts > 0x180000000 && self.last_fixed_timestamp > TS_33BIT_MASK {
            // It looks like ts rolled under. This could happen if we look at timestamps out of order.
            ts | ((self.last_fixed_timestamp >> 33) - 1) << 33
        } else {
            ts | (self.last_fixed_timestamp >> 33) << 33
        };
        self.last_fixed_timestamp
    }

    pub fn write(&mut self, stream_index: usize, mut p: Packet) -> Result<(), EncodeError> {
        if let Some(ref mut dts) = p.dts_90khz {
            *dts = self.fixed_timestamp(*dts);
        } else if let Some(ref mut pts) = p.pts_90khz {
            *pts = self.fixed_timestamp(*pts);
        }

        let last_written_ts = self.streams[stream_index].last_written_ts;
        // if the packet doesn't have a dts, use the pts. if it doesn't have a pts, consider it to have the same timestamp as the last packet
        let ts = p.dts_90khz.or(p.pts_90khz).unwrap_or(last_written_ts);
        // if all other streams have provided packets with timestamps after this packet, we can emit it now
        if ts < self.min_ts_excluding(stream_index) || ts < self.largest_ts_in_buffer.saturating_sub(self.max_buffer_duration_90khz) {
            self.write_muxer_packet(stream_index, p)?;
            self.streams[stream_index].last_written_ts = ts;
        } else {
            // and then we need to see if this enabled any other streams to emit their outputs
            self.largest_ts_in_buffer = self.largest_ts_in_buffer.max(ts);
            self.emit_packets(stream_index, p, ts)?;
        }
        Ok(())
    }

    fn write_muxer_packet(&mut self, stream_index: usize, p: Packet) -> Result<(), EncodeError> {
        let stream = &mut self.streams[stream_index].inner;
        self.inner.write(stream, p)?;
        Ok(())
    }

    fn emit_packets_outside_buffer_duration(&mut self) -> Result<(), EncodeError> {
        let mut packets = vec![];
        for (stream_index, stream) in self.streams.iter_mut().enumerate() {
            while let Some(p) = stream.buffered_packets.front() {
                let ts = p.1;
                if self.largest_ts_in_buffer - ts > self.max_buffer_duration_90khz {
                    packets.push((
                        stream_index,
                        ts,
                        stream.buffered_packets.pop_front().expect("there must be at least one packet to pop"),
                    ));
                } else {
                    break;
                }
            }
        }
        for (stream_index, ts, packet) in packets {
            if let Some(p) = packet.2 {
                self.write_muxer_packet(stream_index, p)?;
            }
            self.streams[stream_index].last_written_ts = ts;
        }
        Ok(())
    }

    fn emit_packets(&mut self, index: usize, p: Packet, ts: u64) -> Result<(), EncodeError> {
        self.streams[index].buffered_packets.push_back((index, ts, None));
        let mut enqueue_packet = true;

        // emit packets outsize max buffer duration
        self.emit_packets_outside_buffer_duration()?;

        let mut packets = vec![];
        loop {
            let mut last_written_ts = 0;
            let mut min_ts_stream_index = 0;
            // find a stream with smallest packet timestamp and emit all packets in this stream if packets
            // in all other streams with timestamps are larger
            let mut emit_packets_found = false;
            if let Some((i, _)) = self
                .streams
                .iter()
                .enumerate()
                .min_by_key(|(_, s)| if let Some(p) = s.buffered_packets.front() { p.1 } else { 0 })
            {
                if !self.streams[i].buffered_packets.is_empty() {
                    let others_min_ts = self.min_ts_excluding(i);
                    let stream = &mut self.streams[i];
                    min_ts_stream_index = i;
                    while let Some(first_packet) = stream.buffered_packets.front() {
                        let first_packet_ts = first_packet.1;
                        if first_packet_ts <= others_min_ts {
                            let first_packet = stream.buffered_packets.pop_front().expect("there must be at least one packet to pop");
                            if first_packet.2.is_none() {
                                enqueue_packet = false;
                            }
                            packets.push(first_packet);
                            last_written_ts = first_packet_ts;
                            emit_packets_found = true;
                        } else {
                            break;
                        }
                    }
                }
            }
            if emit_packets_found {
                self.streams[min_ts_stream_index].last_written_ts = last_written_ts;
            } else {
                break;
            }
        }

        for (i, packet_ts, packet) in packets {
            if let Some(p) = packet {
                self.write_muxer_packet(i, p)?;
                self.streams[i].last_written_ts = packet_ts;
            }
        }
        if enqueue_packet {
            self.streams[index].buffered_packets.pop_back();
            self.streams[index].buffered_packets.push_back((index, ts, Some(p.into_owned())));
        } else {
            self.write_muxer_packet(index, p)?;
        }

        Ok(())
    }

    fn min_ts_excluding(&self, stream_id: usize) -> u64 {
        let mut min_ts = u64::MAX;
        for (_, stream) in self.streams.iter().enumerate().filter(|&(index, _)| index != stream_id) {
            if let Some(p) = stream.buffered_packets.front() {
                min_ts = min_ts.min(p.1);
            } else {
                min_ts = 0;
                break;
            }
        }
        min_ts
    }

    pub fn flush(&mut self) -> Result<(), EncodeError> {
        self.largest_ts_in_buffer = 0;

        let mut packets = vec![];
        self.streams.iter_mut().enumerate().for_each(|(stream_index, stream)| {
            while let Some(p) = stream.buffered_packets.pop_front() {
                packets.push((stream_index, p));
            }
            stream.last_written_ts = 0;
        });

        packets.sort_unstable_by_key(|(_, p)| p.0);
        for (stream_index, packet) in packets {
            let packet = packet.2.expect("emit_packets should never leave None packets in the buffer");
            self.write_muxer_packet(stream_index, packet)?;
        }
        Ok(())
    }
}

impl<W: Write> Drop for InterleavingMuxer<W> {
    fn drop(&mut self) {
        // Errors are ignored when flushing the buffered packets.
        let _ = self.flush();
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{pes, ts};
    use std::{
        cell::{Ref, RefCell},
        fs::File,
        io::{self, Read},
    };

    #[derive(Debug, PartialEq)]
    struct WrittenPacket {
        stream_index: usize,
        pts_90khz: Option<u64>,
        dts_90khz: Option<u64>,
    }

    /// TestWriter is a writer that records the timestamps of all of the PES packets written to it.
    struct TestWriter {
        state: RefCell<TestWriterState>,
    }

    struct TestWriterState {
        buffer: Vec<u8>,
        packets: Vec<WrittenPacket>,
    }

    impl TestWriter {
        pub fn new() -> Self {
            Self {
                state: RefCell::new(TestWriterState {
                    buffer: vec![],
                    packets: vec![],
                }),
            }
        }

        pub fn packets(&self) -> Ref<'_, Vec<WrittenPacket>> {
            Ref::map(self.state.borrow(), |s| &s.packets)
        }
    }

    /// Returns the simplest packet possible with the given PTS.
    fn simple_packet(pts_90khz: u64) -> Packet<'static> {
        Packet {
            data: vec![].into(),
            random_access_indicator: true,
            pts_90khz: Some(pts_90khz),
            dts_90khz: None,
            temi_timeline_descriptors: vec![],
        }
    }

    impl Write for &TestWriter {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            let mut state = self.state.borrow_mut();
            let n = buf.len();
            state.buffer.write_all(buf)?;
            let buffered_bytes = state.buffer.len();
            let remaining_bytes = buffered_bytes % ts::PACKET_LENGTH;
            let incomplete_packet = state.buffer.split_off(buffered_bytes - remaining_bytes);
            let packets = ts::decode_packets(&state.buffer).unwrap();
            let mut completed = vec![];
            for p in packets {
                if p.packet_id >= 0x100 && p.packet_id <= 0x200 {
                    let stream_index = (p.packet_id - 0x100) as usize;
                    if let Some(payload) = p.payload {
                        if p.payload_unit_start_indicator {
                            let (header, _) = pes::PacketHeader::decode(&payload).unwrap();
                            completed.push(WrittenPacket {
                                stream_index,
                                pts_90khz: header.optional_header.as_ref().and_then(|h| h.pts),
                                dts_90khz: header.optional_header.as_ref().and_then(|h| h.dts),
                            });
                        }
                    }
                }
            }
            state.packets.extend(completed.into_iter());
            state.buffer = incomplete_packet;
            Ok(n)
        }

        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    fn get_test_stream_data() -> Vec<u8> {
        let mut buf = Vec::new();
        let mut f = File::open("src/testdata/pro-bowl.ts").unwrap();
        f.read_to_end(&mut buf).unwrap();
        buf
    }

    fn get_pes_packets_from_byte_stream<'a>(data_in: &'a [u8], num_of_pes_packets: usize, included_packet_ids: &'a [u16], sort: bool) -> Vec<pes::Packet<'a>> {
        let ts_packets = ts::decode_packets(data_in).unwrap();
        let mut pes_stream = pes::Stream::new();

        let mut pes_packets = ts_packets
            .into_iter()
            .filter(|p| included_packet_ids.contains(&p.packet_id))
            .flat_map(|p| pes_stream.write(&p).unwrap())
            .take(num_of_pes_packets - 1)
            .collect::<Vec<pes::Packet>>();
        pes_packets.extend(pes_stream.flush().into_iter());
        if sort {
            pes_packets.sort_unstable_by_key(get_pes_packet_ts);
        }
        pes_packets
    }

    fn convert_pes_packet_to_muxer_packet(pes_packet: pes::Packet, random_access_indicator: bool) -> Packet {
        Packet {
            data: pes_packet.data,
            random_access_indicator,
            pts_90khz: pes_packet.header.optional_header.as_ref().and_then(|h| h.pts),
            dts_90khz: pes_packet.header.optional_header.as_ref().and_then(|h| h.dts),
            temi_timeline_descriptors: vec![],
        }
    }

    fn verify_pes_packets_ts_in_sorted_order(pes_packets: &[pes::Packet]) {
        for i in 1..pes_packets.len() {
            let h_prev = pes_packets[i - 1].header.optional_header.as_ref().unwrap();
            let h_cur = pes_packets[i].header.optional_header.as_ref().unwrap();
            assert!(h_prev.dts.or(h_prev.pts).unwrap() <= h_cur.dts.or(h_cur.pts).unwrap())
        }
    }

    fn get_pes_packet_ts(p: &pes::Packet) -> u64 {
        let h = p.header.optional_header.as_ref().unwrap();
        h.dts.or(h.pts).unwrap()
    }

    /// Adds the given number of streams to the muxer with arbitrary configurations.
    fn add_streams<W: Write>(muxer: &mut InterleavingMuxer<W>, count: usize) {
        for _ in 0..count {
            muxer.add_stream(StreamConfig {
                stream_id: 0xe0,
                stream_type: 0x1b,
                data: vec![],
                unbounded_data_length: true,
            });
        }
    }

    #[test]
    fn test_single_stream() {
        let num_of_pes_packets: usize = 200;
        let data_in: Vec<u8> = get_test_stream_data();
        let pes_packets = get_pes_packets_from_byte_stream(&data_in, num_of_pes_packets, &[0x100], true);

        let mut data_out = Vec::new();
        {
            let mut muxer = InterleavingMuxer::new(&mut data_out, Duration::from_millis(0));
            add_streams(&mut muxer, 1);

            let mut random_access_indicator = true;
            for pes_packet in pes_packets {
                muxer.write(0, convert_pes_packet_to_muxer_packet(pes_packet, random_access_indicator)).unwrap();
                random_access_indicator = false;
            }
            let stream = &muxer.streams[0];
            assert!(stream.buffered_packets.is_empty());
            assert!(stream.last_written_ts > 0);
        }

        let reconstructed_pes_packets = get_pes_packets_from_byte_stream(&data_out, num_of_pes_packets, &[0x100], false);
        assert_eq!(reconstructed_pes_packets.len(), num_of_pes_packets);
        verify_pes_packets_ts_in_sorted_order(&reconstructed_pes_packets);
    }

    #[test]
    fn test_one_stream_stop_sending_data() {
        let num_of_pes_packets_per_stream: usize = 200;
        let num_of_streams: usize = 2;
        let data_in = get_test_stream_data();
        let pes_packets = get_pes_packets_from_byte_stream(&data_in, num_of_pes_packets_per_stream * num_of_streams, &[0x100], true);

        let mut pes_packets_array = [VecDeque::new(), VecDeque::new()];
        for (i, pes_packet) in pes_packets.into_iter().enumerate() {
            pes_packets_array[i % num_of_streams as usize].push_back(pes_packet);
        }

        let mut data_out = Vec::new();
        {
            let mut muxer = InterleavingMuxer::new(&mut data_out, Duration::from_millis(20));
            add_streams(&mut muxer, num_of_streams);

            let mut random_access_indicator = true;
            for n in 0..num_of_pes_packets_per_stream {
                for (i, pes_packets) in pes_packets_array.iter_mut().enumerate() {
                    if i == 0 && n >= 3 {
                        continue;
                    }
                    let pes_packet = pes_packets.pop_front().unwrap();
                    muxer.write(i, convert_pes_packet_to_muxer_packet(pes_packet, random_access_indicator)).unwrap();
                }
                random_access_indicator = false;
            }
            assert!(muxer.streams.iter().all(|s| s.last_written_ts > 0));
            assert!(muxer.largest_ts_in_buffer > 0);

            muxer.flush().unwrap();
            assert!(muxer.streams.iter().all(|s| s.buffered_packets.is_empty()));
            assert!(muxer.streams.iter().all(|s| s.last_written_ts == 0));
            assert_eq!(muxer.largest_ts_in_buffer, 0);
        }
        let reconstructed_pes_packets =
            get_pes_packets_from_byte_stream(&data_out, num_of_pes_packets_per_stream * num_of_streams, &[0x100, 0x101, 0x102], false);

        assert_eq!(reconstructed_pes_packets.len(), num_of_pes_packets_per_stream + 3);
        verify_pes_packets_ts_in_sorted_order(&reconstructed_pes_packets);
    }

    #[test]
    fn test_multiple_out_of_sync_streams() {
        let num_of_pes_packets_per_stream: usize = 200;
        let num_of_streams: usize = 3;
        let data_in = get_test_stream_data();
        let pes_packets = get_pes_packets_from_byte_stream(&data_in, num_of_pes_packets_per_stream * num_of_streams, &[0x100], true);

        let mut pes_packets_array = [VecDeque::new(), VecDeque::new(), VecDeque::new()];
        for (i, pes_packet) in pes_packets.into_iter().enumerate() {
            pes_packets_array[i % 3].push_back(pes_packet);
        }

        let mut data_out = Vec::new();
        {
            let mut muxer = InterleavingMuxer::new(&mut data_out, Duration::from_millis(30));
            add_streams(&mut muxer, 3);

            let mut random_access_indicator = true;
            for _ in 0..num_of_pes_packets_per_stream {
                for i in (0..3).rev() {
                    let pes_packet = pes_packets_array[i].pop_front().unwrap();
                    muxer.write(i, convert_pes_packet_to_muxer_packet(pes_packet, random_access_indicator)).unwrap();
                }
                random_access_indicator = false;
            }
            assert!(muxer.streams.iter().all(|s| s.last_written_ts > 0));
            assert!(muxer.largest_ts_in_buffer > 0);

            muxer.flush().unwrap();
            assert!(muxer.streams.iter().all(|s| s.buffered_packets.is_empty()));
            assert!(muxer.streams.iter().all(|s| s.last_written_ts == 0));
            assert_eq!(muxer.largest_ts_in_buffer, 0);
        }
        let reconstructed_pes_packets =
            get_pes_packets_from_byte_stream(&data_out, num_of_pes_packets_per_stream * num_of_streams, &[0x100, 0x101, 0x102], false);
        assert_eq!(reconstructed_pes_packets.len(), num_of_pes_packets_per_stream * num_of_streams);
        verify_pes_packets_ts_in_sorted_order(&reconstructed_pes_packets);
    }

    #[test]
    fn test_timestamp_rolled_over_multiple_times() {
        let step = 0x1001u64;
        let mut ts_vec = vec![];
        let mut f = |mut ts, count| {
            for _ in 0..count {
                ts += step;
                ts_vec.push(ts);
            }
        };
        f(0x1_fffb_0000u64, 10);
        f(0x80002, 10);
        f(0x80000003, 10);
        let timestamps = std::iter::repeat(ts_vec).take(6).flatten().collect::<Vec<u64>>();
        // println!("{:02x?}", timestamps);

        let data_in = get_test_stream_data();
        let pes_packets = get_pes_packets_from_byte_stream(&data_in, timestamps.len(), &[0x100], false);

        let num_of_streams: usize = 2;
        let mut pes_packets_array = [VecDeque::new(), VecDeque::new()];
        for (i, mut pes_packet) in pes_packets.into_iter().enumerate().take(timestamps.len()) {
            let h = pes_packet.header.optional_header.as_mut().unwrap();
            if let Some(ref mut dts) = h.dts {
                *dts = timestamps[i];
            } else if let Some(ref mut pts) = h.pts {
                *pts = timestamps[i];
            }
            pes_packet.header.stream_id += (i % num_of_streams) as u8;
            pes_packets_array[i % num_of_streams].push_back(pes_packet);
        }

        let num_of_pes_packets_per_stream: usize = timestamps.len() / num_of_streams;
        let mut data_out = Vec::new();
        {
            let mut muxer = InterleavingMuxer::new(&mut data_out, Duration::from_millis(10000));
            add_streams(&mut muxer, num_of_streams);

            let mut random_access_indicator = true;
            for _ in 0..num_of_pes_packets_per_stream {
                for i in (0..num_of_streams).rev() {
                    let pes_packet = pes_packets_array[i].pop_front().unwrap();
                    muxer.write(i, convert_pes_packet_to_muxer_packet(pes_packet, random_access_indicator)).unwrap();
                }
                random_access_indicator = false;
            }
            muxer.flush().unwrap();
            assert!(muxer.streams.iter().all(|s| s.buffered_packets.is_empty()));
            assert!(muxer.streams.iter().all(|s| s.last_written_ts == 0));
            assert_eq!(muxer.largest_ts_in_buffer, 0);
        }
        let reconstructed_pes_packets = get_pes_packets_from_byte_stream(&data_out, num_of_pes_packets_per_stream * num_of_streams, &[0x100, 0x101], false);
        assert_eq!(reconstructed_pes_packets.len(), timestamps.len());

        for (p, ts) in reconstructed_pes_packets.iter().zip(timestamps) {
            let h = p.header.optional_header.as_ref().unwrap();
            assert_eq!(h.dts.or(h.pts).unwrap(), ts);
        }
    }

    /// With only one stream, all packets should simply be written immediately, with no buffering.
    #[test]
    fn test_single_stream_latency() {
        let w = TestWriter::new();

        let mut muxer = InterleavingMuxer::new(&w, Duration::from_millis(500));
        add_streams(&mut muxer, 1);

        muxer.write(0, simple_packet(100)).unwrap();
        assert_eq!(w.packets().len(), 1);

        muxer.write(0, simple_packet(200)).unwrap();
        assert_eq!(w.packets().len(), 2);

        muxer.write(0, simple_packet(300)).unwrap();
        assert_eq!(w.packets().len(), 3);
    }

    /// With two streams, one stream will always lead the other. And for the stream that's behind,
    /// packets should always be written through immediately, without buffering.
    #[test]
    fn test_multiple_stream_latency() {
        let w = TestWriter::new();

        let mut muxer = InterleavingMuxer::new(&w, Duration::from_millis(500));
        add_streams(&mut muxer, 2);

        // This first packet should not be written yet. The other stream may have a packet that
        // comes before it.
        muxer.write(0, simple_packet(100)).unwrap();
        assert!(w.packets().is_empty());

        // This packet should be written immediately as the other stream has already provided a
        // packet after it.
        muxer.write(1, simple_packet(50)).unwrap();
        assert_eq!(w.packets().len(), 1);

        // This packet still should not be written yet.
        muxer.write(0, simple_packet(200)).unwrap();
        assert_eq!(w.packets().len(), 1);

        // At this point, we can write both the first packet and this one.
        muxer.write(1, simple_packet(150)).unwrap();
        println!("{:?}", *w.packets());
        assert_eq!(
            *w.packets(),
            vec![
                WrittenPacket {
                    stream_index: 1,
                    pts_90khz: Some(50),
                    dts_90khz: None
                },
                WrittenPacket {
                    stream_index: 0,
                    pts_90khz: Some(100),
                    dts_90khz: None
                },
                WrittenPacket {
                    stream_index: 1,
                    pts_90khz: Some(150),
                    dts_90khz: None
                }
            ]
        );
    }
}
