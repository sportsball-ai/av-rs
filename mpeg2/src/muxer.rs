use super::{pes, ts, EncodeError};
use alloc::vec::Vec;
use core2::io::Write;

struct StreamState {
    stream_type: u8,
    pid: u16,
    data: Vec<u8>,
}

pub struct Muxer<W> {
    w: W,
    next_packet_id: u16,
    pcr_pid: u16,
    did_write_headers: bool,
    last_header_pcr: Option<u64>,
    header_continuity_counter: u8,
    streams: Vec<StreamState>,
}

#[derive(Clone, Debug)]
pub struct StreamConfig {
    pub stream_id: u8,
    pub stream_type: u8,
    pub data: Vec<u8>,

    /// If true, PES headers will indicate a data length of 0, which means packets can be any
    /// length. This is only valid for video streams.
    pub unbounded_data_length: bool,
}

impl<W: Write> Muxer<W> {
    pub fn new(w: W) -> Self {
        Self {
            w,
            next_packet_id: 0x100,
            pcr_pid: 0,
            last_header_pcr: None,
            did_write_headers: false,
            header_continuity_counter: 0,
            streams: vec![],
        }
    }

    pub fn new_stream(&mut self, config: StreamConfig) -> Stream {
        let packet_id = self.next_packet_id;
        self.next_packet_id += 1;
        self.streams.push(StreamState {
            stream_type: config.stream_type,
            pid: packet_id,
            data: config.data,
        });
        self.pcr_pid = packet_id;
        Stream {
            packet_id,
            continuity_counter: 0,
            stream_id: config.stream_id,
            unbounded_data_length: config.unbounded_data_length,
        }
    }

    pub fn write(&mut self, stream: &mut Stream, p: Packet) -> Result<(), EncodeError> {
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
                stream.continuity_counter = (stream.continuity_counter + 1) % 16;
            }
            self.write_ts_packet(ts_packet)?;
        }
        Ok(())
    }

    fn write_ts_packet(&mut self, p: ts::Packet) -> Result<(), EncodeError> {
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
                        .streams
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
}

#[derive(Default)]
pub struct Packet {
    pub data: Vec<u8>,
    pub random_access_indicator: bool,
    pub pts_90khz: Option<u64>,
    pub dts_90khz: Option<u64>,
}

pub struct Stream {
    packet_id: u16,
    continuity_counter: u8,
    stream_id: u8,
    unbounded_data_length: bool,
}

impl Stream {
    pub fn packet_id(&self) -> u16 {
        self.packet_id
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
            let mut muxer = Muxer::new(&mut data_out);
            let mut video_out = muxer.new_stream(StreamConfig {
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
                    muxer
                        .write(
                            &mut video_out,
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
        //File::create("tmp.ts").unwrap().write_all(&data_out).unwrap();
    }
}
