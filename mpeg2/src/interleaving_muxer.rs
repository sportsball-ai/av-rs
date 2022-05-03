use super::{pes, ts, EncodeError};
use crate::muxer::StreamConfig;
use alloc::vec::Vec;
use core2::io::Write;
use std::collections::VecDeque;
use std::time::Duration;

struct StreamState {
    stream_type: u8,
    pid: u16,
    data: Vec<u8>,
}

pub struct InterleavingMuxer<W> {
    w: W,
    next_packet_id: u16,
    pcr_pid: u16,
    did_write_headers: bool,
    last_header_pcr: Option<u64>,
    header_continuity_counter: u8,
    stream_states: Vec<StreamState>,
    max_buffer_duration: u64,
    largest_ts_in_buffer: u64,
    streams: Vec<InterleavingStream>,
}

#[derive(Default)]
pub struct MuxerPacket {
    pub data: Vec<u8>,
    pub random_access_indicator: bool,
    pub pts_90khz: Option<u64>,
    pub dts_90khz: Option<u64>,
}

pub struct InterleavingStream {
    packet_id: u16,
    continuity_counter: u8,
    stream_id: u8,
    unbounded_data_length: bool,
    buffered_packets: VecDeque<MuxerPacket>,
    last_written_ts: u64,
}

impl<W: Write> InterleavingMuxer<W> {
    pub fn new(w: W, max_buffer_duration: Duration) -> Self {
        Self {
            w,
            next_packet_id: 0x100,
            pcr_pid: 0,
            last_header_pcr: None,
            did_write_headers: false,
            header_continuity_counter: 0,
            stream_states: vec![],
            max_buffer_duration: (max_buffer_duration.as_millis() * 90) as u64,
            largest_ts_in_buffer: 0,
            streams: vec![],
        }
    }

    pub fn add_stream(&mut self, config: StreamConfig) {
        let packet_id = self.next_packet_id;
        let stream_state = StreamState {
            stream_type: config.stream_type,
            pid: packet_id,
            data: config.data,
        };
        self.next_packet_id += 1;
        self.stream_states.push(stream_state);
        self.pcr_pid = packet_id;
        let stream = InterleavingStream {
            packet_id,
            continuity_counter: 0,
            stream_id: config.stream_id,
            unbounded_data_length: config.unbounded_data_length,
            buffered_packets: VecDeque::with_capacity(6),
            last_written_ts: 0,
        };
        self.streams.push(stream);
    }

    pub fn write(&mut self, stream_index: usize, p: MuxerPacket) -> Result<(), EncodeError> {
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
                let min_ts = self.min_ts_excluding(self.streams[index].packet_id as usize);
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
                let packet_id = self.streams[min_ts_stream_index].packet_id;
                for packet in packets {
                    self.write_packet(packet_id as usize, packet)?;
                }
                self.streams[min_ts_stream_index].last_written_ts = last_written_ts;
            }
        }
        Ok(())
    }

    fn write_packet(&mut self, packet_id: usize, p: MuxerPacket) -> Result<(), EncodeError> {
        let stream = &mut self.streams[packet_id];
        let pes_packet = pes::Packet {
            header: pes::PacketHeader {
                stream_id: stream.stream_id,
                optional_header: Some(pes::OptionalHeader {
                    data_alignment_indicator: true,
                    pts: p.pts_90khz,
                    dts: p.dts_90khz,
                }),
                data_length: if stream.unbounded_data_length { 0 } else { p.data.len() },
            },
            data: p.data.into(),
        };
        for ts_packet in pes_packet.packetize(pes::PacketizationConfig {
            packet_id: stream.packet_id,
            continuity_counter: stream.continuity_counter,
            random_access_indicator: p.random_access_indicator,
        }) {
            if ts_packet.payload.is_some() {
                let stream = &mut self.streams[packet_id];
                stream.continuity_counter = (stream.continuity_counter + 1) % 16;
            }
            self.write_ts_packet(ts_packet)?;
        }
        Ok(())
    }

    fn write_packets_outside_buffer_duration(&mut self) -> Result<(), EncodeError> {
        let mut packets = vec![];
        for stream in self.streams.iter_mut() {
            while let Some(p) = stream.buffered_packets.front() {
                let ts = p.dts_90khz.or(p.pts_90khz).unwrap_or(stream.last_written_ts);
                if self.largest_ts_in_buffer - ts > self.max_buffer_duration {
                    packets.push((stream.packet_id, stream.buffered_packets.pop_front().unwrap()));
                }
            }
        }
        for (packet_id, packet) in packets {
            self.write_packet(packet_id as usize, packet)?;
        }
        Ok(())
    }

    fn write_ts_packet(&mut self, p: ts::Packet) -> Result<(), EncodeError> {
        // let mut state = self.state.borrow_mut();
        let pcr = p.adaptation_field.as_ref().and_then(|af| af.program_clock_reference_27mhz);

        let mut should_write_headers = !self.did_write_headers;
        if p.packet_id == self.pcr_pid && self.did_write_headers {
            // figure out if we need to repeat the headers
            if let Some(pcr) = pcr {
                if self.last_header_pcr.is_none() {
                    self.last_header_pcr = Some(pcr);
                }
                if let Some(last_pcr) = self.last_header_pcr {
                    // repeat the headers if we've wrapped around or if 90ms has passed
                    if pcr + 27_000_000 < last_pcr || pcr > last_pcr + 2_430_000 {
                        should_write_headers = true;
                    }
                }
            }
        }

        if should_write_headers {
            const PMT_PID: u16 = 0x1000;

            // write the PAT
            {
                let pat = ts::PATData {
                    entries: vec![ts::PATEntry {
                        program_number: 1,
                        program_map_pid: PMT_PID,
                    }],
                };
                let mut encoded = vec![];
                pat.encode(&mut encoded)?;
                let section = ts::TableSyntaxSection {
                    table_id_extension: 1,
                    data: &encoded,
                };
                let mut encoded = vec![];
                section.encode_without_crc(&mut encoded)?;
                let section = ts::TableSection {
                    table_id: ts::TABLE_ID_PAT,
                    section_syntax_indicator: true,
                    data_without_crc: &encoded,
                };
                let mut encoded = vec![];
                ts::encode_table_sections([section], &mut encoded, ts::Packet::max_payload_len(None))?;
                let p = ts::Packet {
                    packet_id: ts::PID_PAT,
                    payload_unit_start_indicator: true,
                    continuity_counter: self.header_continuity_counter,
                    adaptation_field: None,
                    payload: Some(encoded.into()),
                };
                p.encode(&mut self.w)?;
            }

            // write the PMT
            {
                let pmt = ts::PMTData {
                    pcr_pid: self.pcr_pid,
                    elementary_stream_info: self
                        .stream_states
                        .iter()
                        .map(|s| ts::PMTElementaryStreamInfo {
                            elementary_pid: s.pid,
                            stream_type: s.stream_type,
                            data: &s.data,
                        })
                        .collect(),
                };
                let mut encoded = vec![];
                pmt.encode(&mut encoded)?;
                let section = ts::TableSyntaxSection {
                    table_id_extension: 1,
                    data: &encoded,
                };
                let mut encoded = vec![];
                section.encode_without_crc(&mut encoded)?;
                let section = ts::TableSection {
                    table_id: ts::TABLE_ID_PMT,
                    section_syntax_indicator: true,
                    data_without_crc: &encoded,
                };
                let mut encoded = vec![];
                ts::encode_table_sections([section], &mut encoded, ts::Packet::max_payload_len(None))?;
                let p = ts::Packet {
                    packet_id: PMT_PID,
                    payload_unit_start_indicator: true,
                    continuity_counter: self.header_continuity_counter,
                    adaptation_field: None,
                    payload: Some(encoded.into()),
                };
                p.encode(&mut self.w)?;
            }

            self.did_write_headers = true;
            self.last_header_pcr = pcr;
            self.header_continuity_counter = (self.header_continuity_counter + 1) % 16;
        }

        p.encode(&mut self.w)?;
        Ok(())
    }

    fn min_ts_excluding(&self, packet_id: usize) -> u64 {
        let mut min_ts = u64::MAX;
        for s in self.streams.iter().filter(|s| s.packet_id as usize != packet_id) {
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
                            MuxerPacket {
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
