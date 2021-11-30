use super::{pes, ts};
use std::{
    cell::RefCell,
    io::{Result, Write},
};

struct StreamState {
    stream_type: u8,
    pid: u16,
    data: Vec<u8>,
}

struct State<W> {
    w: W,
    next_packet_id: u16,
    did_write_headers: bool,
    streams: Vec<StreamState>,
}

pub struct Muxer<W> {
    state: RefCell<State<W>>,
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
            state: RefCell::new(State {
                w,
                next_packet_id: 0x100,
                did_write_headers: false,
                streams: vec![],
            }),
        }
    }

    pub fn new_stream(&self, config: StreamConfig) -> Stream<'_, W> {
        let mut state = self.state.borrow_mut();
        let packet_id = state.next_packet_id;
        state.next_packet_id += 1;
        state.streams.push(StreamState {
            stream_type: config.stream_type,
            pid: packet_id,
            data: config.data,
        });
        Stream {
            muxer: self,
            packet_id,
            continuity_counter: 0,
            stream_id: config.stream_id,
            unbounded_data_length: config.unbounded_data_length,
        }
    }

    fn write(&self, p: ts::Packet) -> Result<()> {
        let mut state = self.state.borrow_mut();

        if !state.did_write_headers {
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
                    continuity_counter: 0,
                    adaptation_field: None,
                    payload: Some(encoded.into()),
                };
                p.encode(&mut state.w)?;
            }

            // write the PMT
            {
                let pmt = ts::PMTData {
                    pcr_pid: state.streams.first().map(|s| s.pid).unwrap_or(0),
                    elementary_stream_info: state
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
                    continuity_counter: 0,
                    adaptation_field: None,
                    payload: Some(encoded.into()),
                };
                p.encode(&mut state.w)?;
            }

            state.did_write_headers = true;
        }

        p.encode(&mut state.w)?;
        Ok(())
    }
}

#[derive(Default)]
pub struct Packet<'a> {
    pub data: &'a [u8],
    pub random_access_indicator: bool,
    pub pts_90khz: Option<u64>,
    pub dts_90khz: Option<u64>,
}

pub struct Stream<'a, W> {
    muxer: &'a Muxer<W>,
    packet_id: u16,
    continuity_counter: u8,
    stream_id: u8,
    unbounded_data_length: bool,
}

impl<'a, W: Write> Stream<'a, W> {
    pub fn write(&mut self, p: Packet) -> Result<()> {
        let pes_packet = pes::Packet {
            header: pes::PacketHeader {
                stream_id: self.stream_id,
                optional_header: Some(pes::OptionalHeader {
                    data_alignment_indicator: true,
                    pts: p.pts_90khz,
                    dts: p.dts_90khz,
                }),
                data_length: if self.unbounded_data_length { 0 } else { p.data.len() },
            },
            data: p.data.into(),
        };
        for ts_packet in pes_packet.packetize(pes::PacketizationConfig {
            packet_id: self.packet_id,
            continuity_counter: self.continuity_counter,
            random_access_indicator: p.random_access_indicator,
        }) {
            if ts_packet.payload.is_some() {
                self.continuity_counter = (self.continuity_counter + 1) % 16;
            }
            self.muxer.write(ts_packet)?;
        }
        Ok(())
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
            let muxer = Muxer::new(&mut data_out);
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
                    video_out
                        .write(Packet {
                            data: &frame.data,
                            random_access_indicator,
                            pts_90khz: frame.header.optional_header.as_ref().and_then(|h| h.pts),
                            dts_90khz: frame.header.optional_header.as_ref().and_then(|h| h.dts),
                        })
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
