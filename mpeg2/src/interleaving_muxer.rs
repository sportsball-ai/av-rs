use super::{pes, ts, EncodeError};
use crate::muxer::{Muxer, Packet, Stream, StreamConfig, StreamState};
use alloc::vec::Vec;
use core2::io::Write;
use std::collections::VecDeque;
use std::time::Duration;

pub struct InterleavingMuxer<W> {
    muxer: Muxer<W>,
    max_buffer_duration: u64,
    largest_ts_in_buffer: u64,
    streams: Vec<InterleavingStream>,
}

pub struct InterleavingStream {
    wrapper: Stream,
    buffered_packets: VecDeque<Packet>,
    last_written_ts: u64,
}

impl<W: Write> InterleavingMuxer<W> {
    pub fn new(w: W, max_buffer_duration: Duration) -> Self {
        let muxer = Muxer {
            w,
            next_packet_id: 0x100,
            pcr_pid: 0,
            last_header_pcr: None,
            did_write_headers: false,
            header_continuity_counter: 0,
            streams: vec![],
        };
        Self {
            muxer,
            max_buffer_duration: (max_buffer_duration.as_millis() * 90) as u64,
            largest_ts_in_buffer: 0,
            streams: vec![],
        }
    }

    pub fn add_stream(&mut self, config: StreamConfig) {
        let packet_id = self.muxer.next_packet_id;
        let stream_state = StreamState {
            stream_type: config.stream_type,
            pid: packet_id,
            data: config.data,
        };
        self.muxer.next_packet_id += 1;
        self.muxer.streams.push(stream_state);
        self.muxer.pcr_pid = packet_id;
        let internal_stream = Stream {
            packet_id,
            continuity_counter: 0,
            stream_id: config.stream_id,
            unbounded_data_length: config.unbounded_data_length,
        };
        let interleaving_stream = InterleavingStream {
            wrapper: internal_stream,
            buffered_packets: VecDeque::with_capacity(6),
            last_written_ts: 0,
        };
        self.streams.push(interleaving_stream);
    }

    pub fn write(&mut self, stream_index: usize, p: Packet) -> Result<(), EncodeError> {
        let last_written_ts = self.streams[stream_index].last_written_ts;
        // if the packet doesn't have a dts, use the pts. if it doesn't have a pts, consider it to have the same timestamp as the last packet
        let ts = p.dts_90khz.or(p.pts_90khz);
        // if all other streams have provided packets with timestamps after this packet, we can emit it now
        if self.min_ts_excluding(stream_index) > ts.unwrap_or(last_written_ts) {
            self.write_packet(stream_index, p)?;
            if let Some(t) = ts {
                self.streams[stream_index].last_written_ts = t;
            }
        } else {
            // and then we need to see if this enabled any other streams to emit their outputs
            self.streams[stream_index].buffered_packets.push_back(p);
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
                        u64::MAX
                    }
                })
            {
                let min_ts = self.min_ts_excluding(self.streams[index].wrapper.packet_id as usize);
                let stream = &mut self.streams[index];
                min_ts_stream_index = index;
                while let Some(p) = stream.buffered_packets.front() {
                    let ts = p.dts_90khz.or(p.pts_90khz).unwrap_or(stream.last_written_ts);
                    if ts <= min_ts {
                        packets.push(stream.buffered_packets.pop_front().unwrap());
                        last_written_ts = ts;
                    } else {
                        break;
                    }
                }
            }
            if !packets.is_empty() {
                for packet in packets {
                    self.write_packet(min_ts_stream_index, packet)?;
                }
                self.streams[min_ts_stream_index].last_written_ts = last_written_ts;
            }
        }
        Ok(())
    }

    fn write_packet(&mut self, stream_index: usize, p: Packet) -> Result<(), EncodeError> {
        let stream = &mut self.streams[stream_index].wrapper;
        self.muxer.write(stream, p)?;
        Ok(())
    }

    fn write_packets_outside_buffer_duration(&mut self) -> Result<(), EncodeError> {
        let mut packets = vec![];
        for stream in self.streams.iter_mut() {
            while let Some(p) = stream.buffered_packets.front() {
                let ts = p.dts_90khz.or(p.pts_90khz).unwrap_or(stream.last_written_ts);
                if self.largest_ts_in_buffer - ts > self.max_buffer_duration {
                    packets.push((stream.wrapper.packet_id, stream.buffered_packets.pop_front().unwrap()));
                }
            }
        }
        for (packet_id, packet) in packets {
            self.write_packet(packet_id as usize, packet)?;
        }
        Ok(())
    }

    fn min_ts_excluding(&self, packet_id: usize) -> u64 {
        let mut min_ts = u64::MAX;
        for s in self.streams.iter().filter(|s| s.wrapper.packet_id as usize != packet_id) {
            if let Some(p) = s.buffered_packets.front() {
                min_ts = min_ts.min(p.dts_90khz.or(p.pts_90khz).unwrap_or(0));
            }
        }
        min_ts
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::{fs::File, io::Read};

    #[test]
    fn test_muxer() {
        let data_in = {
            let mut buf = Vec::new();
            let mut f = File::open("src/testdata/pro-bowl.ts").unwrap();
            f.read_to_end(&mut buf).unwrap();
            buf
        };
        let packets_in = ts::decode_packets(&data_in).unwrap();
        let mut video_in = pes::Stream::new();

        let mut data_out = Vec::new();
        {
            let mut interleaving_muxer = InterleavingMuxer::new(&mut data_out, Duration::from_millis(10));
            interleaving_muxer.add_stream(StreamConfig {
                stream_id: 0xe0,
                stream_type: 0x1b,
                data: vec![],
                unbounded_data_length: true,
            });
            let mut random_access_indicator = true;

            for p in packets_in {
                if p.packet_id != 0x0100 {
                    continue;
                }

                for frame in video_in.write(&p).unwrap() {
                    interleaving_muxer
                        .write(
                            0,
                            Packet {
                                data: frame.data.into(),
                                random_access_indicator,
                                pts_90khz: frame.header.optional_header.as_ref().and_then(|h| h.pts),
                                dts_90khz: frame.header.optional_header.as_ref().and_then(|h| h.dts),
                            },
                        )
                        .unwrap();
                    random_access_indicator = false;
                }
            }
        }

        assert!(data_out.len() >= 8000000);

        // uncomment this to write to a file for testing with other programs
        // File::create("tmp.ts").unwrap().write_all(&data_out).unwrap();
    }
}
