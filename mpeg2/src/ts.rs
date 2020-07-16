use std::error::Error;

pub type Result<T> = std::result::Result<T, Box<dyn Error + Send + Sync>>;

pub const PID_PAT: u16 = 0x00;

#[derive(Debug, PartialEq)]
pub struct Packet<'a> {
    pub packet_id: u16,
    pub payload_unit_start_indicator: bool,
    pub adaptation_field: Option<AdaptationField>,
    pub payload: Option<&'a [u8]>,
}

#[derive(Debug, PartialEq)]
pub struct AdaptationField {
    pub length: u8,
    pub random_access_indicator: bool,
    pub program_clock_reference_27mhz: Option<u64>,
}

pub const TABLE_ID_PAT: u8 = 0;
pub const TABLE_ID_PMT: u8 = 2;

#[derive(Debug, PartialEq)]
pub struct TableSection<'a> {
    pub table_id: u8,
    pub section_syntax_indicator: bool,
    pub data: &'a [u8],
}

const TABLE_SECTION_HEADER_LENGTH: usize = 3;

impl<'a> TableSection<'a> {
    pub fn decode(buf: &'a [u8]) -> Result<Self> {
        if buf.len() < TABLE_SECTION_HEADER_LENGTH {
            bail!("not enough bytes for table section header")
        }
        let length = ((buf[1] & 0x03) as usize) << 8 | buf[2] as usize;
        if buf.len() < TABLE_SECTION_HEADER_LENGTH + length {
            bail!("not enough bytes for data")
        }
        Ok(Self {
            table_id: buf[0],
            section_syntax_indicator: buf[1] & 0x80 != 0,
            data: &buf[TABLE_SECTION_HEADER_LENGTH..TABLE_SECTION_HEADER_LENGTH + length],
        })
    }

    pub fn decode_syntax_section(&self) -> Result<TableSyntaxSection> {
        TableSyntaxSection::decode(self.data)
    }
}

#[derive(Debug, PartialEq)]
pub struct TableSyntaxSection<'a> {
    pub table_id_extension: u16,
    pub data: &'a [u8],
}

impl<'a> TableSyntaxSection<'a> {
    pub fn decode(buf: &'a [u8]) -> Result<Self> {
        if buf.len() < 9 {
            bail!("not enough bytes for table syntax section")
        }
        Ok(Self {
            table_id_extension: (buf[0] as u16) << 8 | buf[1] as u16,
            data: &buf[5..buf.len() - 4],
        })
    }
}

#[derive(Debug, PartialEq)]
pub struct PATEntry {
    pub program_number: u16,
    pub program_map_pid: u16,
}

impl PATEntry {
    pub fn decode(buf: &[u8]) -> Result<Self> {
        if buf.len() != 4 {
            bail!("unexpected number of bytes for pat entry")
        }
        Ok(Self {
            program_number: (buf[0] as u16) << 8 | buf[1] as u16,
            program_map_pid: ((buf[2] & 0x1f) as u16) << 8 | buf[3] as u16,
        })
    }
}

#[derive(Debug, PartialEq)]
pub struct PATData {
    pub entries: Vec<PATEntry>,
}

impl PATData {
    pub fn decode(buf: &[u8]) -> Result<Self> {
        Ok(Self {
            entries: buf.chunks(4).map(PATEntry::decode).collect::<Result<Vec<PATEntry>>>()?,
        })
    }
}

#[derive(Debug, PartialEq)]
pub struct PMTElementaryStreamInfo<'a> {
    pub stream_type: u8,
    pub elementary_pid: u16,
    pub data: &'a [u8],
}

const PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH: usize = 5;

impl<'a> PMTElementaryStreamInfo<'a> {
    pub fn decode(buf: &'a [u8]) -> Result<Self> {
        if buf.len() < PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH {
            bail!("not enough bytes for pmt elementary stream info header")
        }
        let length = ((buf[3] & 0x03) as usize) << 8 | buf[4] as usize;
        if buf.len() < PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH + length {
            bail!("not enough bytes for pmt elementary stream info")
        }
        Ok(Self {
            stream_type: buf[0],
            elementary_pid: ((buf[1] & 0x1f) as u16) << 8 | buf[2] as u16,
            data: &buf[PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH..PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH + length],
        })
    }
}

#[derive(Debug, PartialEq)]
pub struct PMTData<'a> {
    pub elementary_stream_info: Vec<PMTElementaryStreamInfo<'a>>,
}

const PMT_HEADER_LENGTH: usize = 4;

impl<'a> PMTData<'a> {
    pub fn decode(buf: &'a [u8]) -> Result<Self> {
        if buf.len() < PMT_HEADER_LENGTH {
            bail!("not enough bytes for pmt header")
        }
        let descs_length = ((buf[2] & 0x03) as usize) << 8 | buf[3] as usize;
        if buf.len() < PMT_HEADER_LENGTH + descs_length {
            bail!("not enough bytes for pmt program descriptors")
        }
        Ok(Self {
            elementary_stream_info: {
                let mut infos = Vec::new();
                let mut buf = &buf[PMT_HEADER_LENGTH + descs_length..];
                while buf.len() > 0 {
                    let info = PMTElementaryStreamInfo::decode(buf)?;
                    buf = &buf[PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH + info.data.len()..];
                    infos.push(info);
                }
                infos
            },
        })
    }
}

pub const PACKET_LENGTH: usize = 188;

impl<'a> Packet<'a> {
    pub fn decode(buf: &'a [u8]) -> Result<Self> {
        if buf.len() != PACKET_LENGTH {
            bail!("incorrect packet length")
        }

        if buf[0] != 0x47 {
            bail!("incorrect sync byte")
        }

        let packet_id = ((buf[1] & 0x1f) as u16) << 8 | buf[2] as u16;

        let adaptation_field_control = buf[3] & 0x30;

        let adaptation_field = if adaptation_field_control & 0x20 != 0 && buf[4] >= 1 {
            let length = buf[4] as usize;
            Some(AdaptationField {
                length: if length > PACKET_LENGTH - 5 {
                    bail!("adaptation field length too long")
                } else {
                    length as _
                },
                random_access_indicator: buf[5] & 0x40 != 0,
                program_clock_reference_27mhz: if length >= 7 && buf[5] & 0x10 != 0 {
                    let base = (buf[6] as u64) << 25 | (buf[7] as u64) << 17 | (buf[8] as u64) << 9 | (buf[9] as u64) << 1 | (buf[10] as u64) >> 7;
                    let ext = (buf[10] as u64) & 0x80 << 1 | (buf[11] as u64);
                    Some(base * 300 + ext)
                } else {
                    None
                },
            })
        } else {
            None
        };

        let payload = if adaptation_field_control & 0x10 != 0 {
            Some(match &adaptation_field {
                Some(f) => &buf[4 + 1 + f.length as usize..],
                None => &buf[4..],
            })
        } else {
            None
        };

        Ok(Self {
            packet_id,
            payload_unit_start_indicator: buf[1] & 0x40 != 0,
            adaptation_field,
            payload,
        })
    }

    pub fn decode_table_sections(&self) -> Result<Vec<TableSection<'a>>> {
        let payload = match self.payload {
            Some(p) => p,
            None => return Ok(vec![]),
        };

        if !self.payload_unit_start_indicator || payload.len() == 0 {
            return Ok(vec![]);
        }

        let padding = payload[0] as usize;
        if 1 + padding > payload.len() {
            bail!("padding too long for payload");
        }

        let mut ret = Vec::new();

        let mut buf = &payload[1 + padding..];
        while buf.len() > 0 && buf[0] != 0xff {
            let section = TableSection::decode(buf)?;
            buf = &buf[TABLE_SECTION_HEADER_LENGTH + section.data.len()..];
            ret.push(section);
        }

        Ok(ret)
    }
}

pub fn decode_packets(buf: &[u8]) -> Result<Vec<Packet>> {
    buf.chunks(PACKET_LENGTH).map(Packet::decode).collect()
}

#[cfg(test)]
mod test {
    use super::*;
    use std::{fs::File, io::Read};

    #[test]
    fn test_decode() {
        let mut f = File::open("src/testdata/pro-bowl.ts").unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();
        let packets = decode_packets(&buf).unwrap();

        assert_eq!(packets.len(), 47128);

        assert_eq!(
            packets[3].adaptation_field,
            Some(AdaptationField {
                length: 7,
                random_access_indicator: true,
                program_clock_reference_27mhz: Some(18900000),
            })
        );

        let mut last_pcr = 0;
        let mut rais = 0;
        for p in &packets {
            // PAT
            if p.packet_id == PID_PAT {
                let table_sections = p.decode_table_sections().unwrap();
                assert_eq!(table_sections.len(), 1);
                assert_eq!(table_sections[0].table_id, TABLE_ID_PAT);

                let syntax_section = table_sections[0].decode_syntax_section().unwrap();
                assert_eq!(syntax_section.table_id_extension, 1);

                let pat = PATData::decode(syntax_section.data).unwrap();

                assert_eq!(
                    pat,
                    PATData {
                        entries: vec![PATEntry {
                            program_number: 1,
                            program_map_pid: 0x1000,
                        }]
                    }
                );
            }

            // PMT
            if p.packet_id == 0x1000 {
                let table_sections = p.decode_table_sections().unwrap();
                assert_eq!(table_sections.len(), 1);
                assert_eq!(table_sections[0].table_id, TABLE_ID_PMT);

                let syntax_section = table_sections[0].decode_syntax_section().unwrap();
                assert_eq!(syntax_section.table_id_extension, 1);

                let pmt = PMTData::decode(syntax_section.data).unwrap();
                assert_eq!(pmt.elementary_stream_info.len(), 2);
            }

            if let Some(f) = &p.adaptation_field {
                if f.random_access_indicator {
                    rais += 1;
                }
                if let Some(pcr) = f.program_clock_reference_27mhz {
                    assert_eq!(pcr > last_pcr, true);
                    last_pcr = pcr;
                }
            }
        }
        assert_eq!(rais, 62);
        assert_eq!(last_pcr, 286917900);
    }
}
