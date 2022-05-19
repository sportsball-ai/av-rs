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
    buffered_packets: VecDeque<Packet<'static>>,
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
        let ts = p.dts_90khz.or(p.pts_90khz);
        // if all other streams have provided packets with timestamps after this packet, we can emit it now
        if self.min_ts_excluding(stream_index) > ts.unwrap_or(last_written_ts) {
            self.write_muxer_packet(stream_index, p)?;
            if let Some(t) = ts {
                self.streams[stream_index].last_written_ts = t;
            }
        } else {
            // and then we need to see if this enabled any other streams to emit their outputs
            self.streams[stream_index].buffered_packets.push_back(p.into_owned());
            if let Some(t) = ts {
                self.largest_ts_in_buffer = self.largest_ts_in_buffer.max(t);
            }
            self.write_packets_outside_buffer_duration()?;

            // find a stream with smallest packet timestamp and emit all packets in this stream if packets
            // in all other streams with timestamps are larger
            let mut packets = vec![];
            let mut min_ts_stream_index = 0;
            let mut last_written_ts = 0;
            if let Some((index, _)) = self
                .streams
                .iter()
                .enumerate()
                .filter(|(index, _)| *index != stream_index)
                .min_by_key(|(_, s)| {
                    if let Some(p) = s.buffered_packets.front() {
                        p.dts_90khz.or(p.pts_90khz).unwrap_or(s.last_written_ts)
                    } else {
                        0
                    }
                })
            {
                if !self.streams[index].buffered_packets.is_empty() {
                    let others_min_ts = self.min_ts_excluding(index);
                    let stream = &mut self.streams[index];
                    min_ts_stream_index = index;
                    while let Some(p) = stream.buffered_packets.front() {
                        let ts = p.dts_90khz.or(p.pts_90khz).unwrap_or(stream.last_written_ts);
                        if ts <= others_min_ts {
                            packets.push(stream.buffered_packets.pop_front().expect("there must be at least one packet to pop"));
                            last_written_ts = ts;
                        } else {
                            break;
                        }
                    }
                }
            }
            if !packets.is_empty() {
                for packet in packets {
                    self.write_muxer_packet(min_ts_stream_index, packet)?;
                }
                self.streams[min_ts_stream_index].last_written_ts = last_written_ts;
            }
        }
        Ok(())
    }

    fn write_muxer_packet(&mut self, stream_index: usize, p: Packet) -> Result<(), EncodeError> {
        let stream = &mut self.streams[stream_index].inner;
        // no need to reset rollovered dts or ptd because only the lower 33 bits are considered when it's written to TS packet
        self.inner.write(stream, p)?;
        Ok(())
    }

    fn write_packets_outside_buffer_duration(&mut self) -> Result<(), EncodeError> {
        let mut packets = vec![];
        for (stream_index, stream) in self.streams.iter_mut().enumerate() {
            while let Some(p) = stream.buffered_packets.front() {
                let ts = p.dts_90khz.or(p.pts_90khz).unwrap_or(stream.last_written_ts);
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
            self.write_muxer_packet(stream_index, packet)?;
            self.streams[stream_index].last_written_ts = ts;
        }
        Ok(())
    }

    fn min_ts_excluding(&self, stream_id: usize) -> u64 {
        let mut min_ts = u64::MAX;
        for (_, stream) in self.streams.iter().enumerate().filter(|&(index, _)| index != stream_id) {
            if let Some(p) = stream.buffered_packets.front() {
                min_ts = min_ts.min(p.dts_90khz.or(p.pts_90khz).unwrap_or(stream.last_written_ts));
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
                packets.push((stream_index, stream.last_written_ts, p));
            }
            stream.last_written_ts = 0;
        });

        packets.sort_unstable_by_key(|(_, ts, p)| p.dts_90khz.or(p.pts_90khz).unwrap_or(*ts));
        for (stream_index, _, packet) in packets {
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
    use std::{fs::File, io::Read};

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
            temi: vec![],
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

    #[test]
    fn test_single_stream() {
        let num_of_pes_packets: usize = 200;
        let data_in: Vec<u8> = get_test_stream_data();
        let pes_packets = get_pes_packets_from_byte_stream(&data_in, num_of_pes_packets, &[0x100], true);

        let mut data_out = Vec::new();
        {
            let mut muxer = InterleavingMuxer::new(&mut data_out, Duration::from_millis(0));
            muxer.add_stream(StreamConfig {
                stream_id: 0xe0,
                stream_type: 0x1b,
                data: vec![],
                unbounded_data_length: true,
            });
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
            for i in 0..num_of_streams {
                muxer.add_stream(StreamConfig {
                    stream_id: 0xe0 + i as u8,
                    stream_type: 0x1b + i as u8,
                    data: vec![],
                    unbounded_data_length: true,
                });
            }
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
            for i in 0..3 {
                muxer.add_stream(StreamConfig {
                    stream_id: 0xe0 + i,
                    stream_type: 0x1b + i,
                    data: vec![],
                    unbounded_data_length: true,
                });
            }
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
            for i in 0..num_of_streams {
                muxer.add_stream(StreamConfig {
                    stream_id: 0xe0 + i as u8,
                    stream_type: 0x1b + i as u8,
                    data: vec![],
                    unbounded_data_length: true,
                });
            }
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
}
