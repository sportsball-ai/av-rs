use super::{DecodeError, EncodeError};
use crate::bitstream::{Bitstream, BitstreamWriter, Decode};
use crate::temi::{TEMITimelineDescriptor, AF_DESCR_TAG_TIMELINE};
use alloc::{borrow::Cow, vec::Vec};
use core2::io::Write;

pub const PID_PAT: u16 = 0x00;

#[derive(Debug, PartialEq)]
pub struct Packet<'a> {
    pub packet_id: u16,
    pub payload_unit_start_indicator: bool,
    pub continuity_counter: u8,
    pub adaptation_field: Option<AdaptationField>,
    pub payload: Option<Cow<'a, [u8]>>,
}

#[derive(Debug, Default, PartialEq)]
pub struct AdaptationField {
    pub discontinuity_indicator: Option<bool>,
    pub random_access_indicator: Option<bool>,
    pub program_clock_reference_27mhz: Option<u64>,
    pub private_data_types: Vec<u8>,
    pub temi_timeline_descriptors: Vec<TEMITimelineDescriptor>,
}

impl AdaptationField {
    pub fn decode(buf: &[u8]) -> Result<Self, DecodeError> {
        let mut bs = Bitstream::new(buf);
        let mut af = Self::default();
        let af_length = bs.read_u8();
        if bs.remaining_bytes() < af_length as usize {
            return Err(DecodeError::new("adaptation field length too long"));
        } else if af_length > 0 {
            af.discontinuity_indicator = Some(bs.read_boolean());
            af.random_access_indicator = Some(bs.read_boolean());
            bs.skip_bits(1); // elementary_stream_priority_indicator

            let pcr_flag = bs.read_boolean();
            let opcr_flag = bs.read_boolean();
            let splicing_point_flag = bs.read_boolean();
            let transport_private_data_flag = bs.read_boolean();
            let adaptation_extension_flag = bs.read_boolean();

            af.program_clock_reference_27mhz = if af_length >= 7 && pcr_flag {
                let base = (bs.read_u32() as u64) << 1 | bs.read_bit() as u64;
                bs.skip_bits(6);
                let ext = (bs.read_bit() as u64) << 8 | bs.read_u8() as u64;
                Some(base * 300 + ext)
            } else {
                None
            };

            if opcr_flag {
                // skip OPCR
                bs.skip_bytes(6);
            }
            if splicing_point_flag {
                // skip splice_countdown
                bs.skip_bytes(1);
            }
            if transport_private_data_flag {
                if bs.remaining_bytes() == 0 {
                    return Err(DecodeError::new("adaptation field too short for transport_private_data_length"));
                }
                let transport_private_data_length = bs.read_u8();
                if transport_private_data_length as usize > bs.remaining_bytes() {
                    return Err(DecodeError::new("transport private data length too long"));
                }
                af.private_data_types = bs.read_n_bytes(transport_private_data_length as usize).to_vec();
            }
            if adaptation_extension_flag {
                if bs.remaining_bytes() < 2 {
                    return Err(DecodeError::new("adaptation field too short for adaptation field extension"));
                }
                let adaptation_field_extension_length = bs.read_u8();
                if adaptation_field_extension_length == 0 {
                    return Err(DecodeError::new("invalid adaptation_field_extension_length"));
                }

                let ltw_flag = bs.read_boolean();
                let piecewise_rate_flag = bs.read_boolean();
                let seamless_splice_flag = bs.read_boolean();
                let af_descriptor_not_present_flag = bs.read_boolean();
                bs.skip_bits(4);

                if ltw_flag {
                    // skip ltw
                    bs.skip_bytes(2);
                }
                if piecewise_rate_flag {
                    // skip piecewise_rate
                    bs.skip_bytes(3);
                }
                if seamless_splice_flag {
                    // skip seamless_splice
                    bs.skip_bytes(5);
                }

                if !af_descriptor_not_present_flag {
                    // AF descriptors
                    if bs.remaining_bytes() + 1 < adaptation_field_extension_length as usize {
                        return Err(DecodeError::new("adaptation descriptor field length too long"));
                    }
                    af.temi_timeline_descriptors = vec![];
                    let mut af_descr_bitstream = bs.wrapped_stream(adaptation_field_extension_length as usize - 1);
                    while af_descr_bitstream.remaining_bytes() >= 2 {
                        let af_descr_tag = af_descr_bitstream.read_u8();
                        let af_descr_length = af_descr_bitstream.read_u8();
                        if af_descr_tag == AF_DESCR_TAG_TIMELINE {
                            let descr = TEMITimelineDescriptor::decode(&mut af_descr_bitstream.wrapped_stream(af_descr_length as usize))?;
                            af_descr_bitstream.skip_bytes(af_descr_length as usize);
                            af.temi_timeline_descriptors.push(descr);
                        } else {
                            // Other types of AF descriptors are ignored
                            af_descr_bitstream.skip_bytes(af_descr_length as usize);
                        }
                    }
                }
            }
        }
        Ok(af)
    }

    pub fn encoded_len(&self) -> usize {
        let mut len = 1;
        if self.discontinuity_indicator.is_some() || self.random_access_indicator.is_some() || self.program_clock_reference_27mhz.is_some() {
            len += 1;
        }
        if self.program_clock_reference_27mhz.is_some() {
            len += 6;
        }
        if !self.private_data_types.is_empty() {
            len += 1 + self.private_data_types.len();
        }
        if !self.temi_timeline_descriptors.is_empty() {
            len += 2;
            for timeline_descriptor in &self.temi_timeline_descriptors {
                len += timeline_descriptor.encoded_len();
            }
        }
        len
    }

    pub fn encode<W: Write>(&self, mut w: W, pad_to_length: usize) -> Result<usize, EncodeError> {
        let has_af_flags = pad_to_length >= 2
            || self.program_clock_reference_27mhz.is_some()
            || !self.private_data_types.is_empty()
            || !self.temi_timeline_descriptors.is_empty();

        let pcr_length = if self.program_clock_reference_27mhz.is_some() { 6 } else { 0 };
        let transport_private_data_length = if self.private_data_types.is_empty() {
            0
        } else {
            self.private_data_types.len() + 1
        };
        let adaptation_field_extension_length = match self.temi_timeline_descriptors.iter().map(|temi| temi.encoded_len()).sum::<usize>() {
            0 => 0,
            n => n + 1,
        };
        if adaptation_field_extension_length > 0xff {
            return Err(EncodeError::other("adaptation_field_extension_length overflow"));
        }

        let af_length_no_padding = if has_af_flags { 1 } else { 0 }
            + pcr_length
            + transport_private_data_length
            + if adaptation_field_extension_length > 1 {
                adaptation_field_extension_length + 1
            } else {
                0
            };

        let mut buf = vec![0u8; af_length_no_padding + 1];
        let mut bs = BitstreamWriter::new(&mut buf[1..]);

        if has_af_flags {
            bs.write_boolean(self.discontinuity_indicator.unwrap_or(false));
            bs.write_boolean(self.random_access_indicator.unwrap_or(false));
            bs.skip_n_bits(1);
            bs.write_boolean(self.program_clock_reference_27mhz.is_some());
            bs.skip_n_bits(2); // splicing_point_flag and transport_private_data_flag are ignored
            bs.write_boolean(!self.private_data_types.is_empty());
            bs.write_boolean(!self.temi_timeline_descriptors.is_empty());
        }

        if let Some(program_clock_reference_27mhz) = self.program_clock_reference_27mhz {
            let base = program_clock_reference_27mhz / 300;
            let ext = program_clock_reference_27mhz % 300;
            bs.write_u32((base >> 1) as u32);
            bs.write_bit((base & 1) as u8);
            bs.write_n_bits(0b11_1111, 6);
            bs.write_bit((ext >> 8) as u8);
            bs.write_u8(ext as u8);
        }

        if !self.private_data_types.is_empty() {
            bs.write_u8(self.private_data_types.len() as u8);
            for data_type in &self.private_data_types {
                bs.write_u8(*data_type);
            }
        }

        if !self.temi_timeline_descriptors.is_empty() {
            bs.write_u8(adaptation_field_extension_length as u8);
            bs.skip_n_bits(8);
            for descr in &self.temi_timeline_descriptors {
                descr.encode(&mut bs)?;
            }
        }

        if af_length_no_padding + 1 < pad_to_length {
            buf[0] = (pad_to_length - 1) as _;
            w.write_all(&buf)?;
            let padding = vec![0xff; pad_to_length - af_length_no_padding - 1];
            w.write_all(&padding)?;
            Ok(pad_to_length)
        } else {
            buf[0] = af_length_no_padding as _;
            w.write_all(&buf)?;
            Ok(af_length_no_padding + 1)
        }
    }
}

pub const TABLE_ID_PAT: u8 = 0;
pub const TABLE_ID_PMT: u8 = 2;

#[derive(Debug, PartialEq)]
pub struct TableSection<'a> {
    pub table_id: u8,
    pub section_syntax_indicator: bool,
    pub data_without_crc: &'a [u8],
}

const TABLE_SECTION_HEADER_LENGTH: usize = 3;

pub fn encode_table_sections<'a, I: IntoIterator<Item = TableSection<'a>>, W: Write>(
    sections: I,
    mut w: W,
    pad_to_length: usize,
) -> Result<usize, EncodeError> {
    w.write_all(&[0])?;
    let mut len = 1;
    for section in sections.into_iter() {
        len += section.encode(&mut w)?;
    }
    if len < pad_to_length {
        let padding = vec![0xff; pad_to_length - len];
        w.write_all(&padding)?;
        len = pad_to_length;
    }
    Ok(len)
}

impl<'a> TableSection<'a> {
    pub fn decode(buf: &'a [u8]) -> Result<Self, DecodeError> {
        if buf.len() < TABLE_SECTION_HEADER_LENGTH {
            return Err(DecodeError::new("not enough bytes for table section header"));
        }
        let mut length = ((buf[1] & 0x03) as usize) << 8 | buf[2] as usize;
        if buf.len() < TABLE_SECTION_HEADER_LENGTH + length {
            return Err(DecodeError::new("not enough bytes for data"));
        }
        if length >= 4 {
            // chop off the crc
            length -= 4;
        }
        Ok(Self {
            table_id: buf[0],
            section_syntax_indicator: buf[1] & 0x80 != 0,
            data_without_crc: &buf[TABLE_SECTION_HEADER_LENGTH..TABLE_SECTION_HEADER_LENGTH + length],
        })
    }

    pub fn decode_syntax_section(&self) -> Result<TableSyntaxSection, DecodeError> {
        TableSyntaxSection::decode_without_crc(self.data_without_crc)
    }

    pub fn encoded_len(&self) -> usize {
        TABLE_SECTION_HEADER_LENGTH
            + self.data_without_crc.len()
            + if self.data_without_crc.is_empty() {
                0
            } else {
                // add 4 bytes for the crc
                4
            }
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        let mut buf = [0u8; TABLE_SECTION_HEADER_LENGTH];
        buf[0] = self.table_id;

        let data_len = self.data_without_crc.len()
            + if self.data_without_crc.is_empty() {
                0
            } else {
                // add 4 bytes for the crc
                4
            };

        buf[1] = 0b00110000 | (data_len >> 8) as u8;
        if self.section_syntax_indicator {
            buf[1] |= 0b10000000;
        } else {
            buf[1] |= 0b01000000;
        }

        buf[2] = data_len as _;

        w.write_all(&buf)?;

        if !self.data_without_crc.is_empty() {
            let crc = crc::Crc::<u32>::new(&crc::CRC_32_MPEG_2);
            let mut crc = crc.digest();
            crc.update(&buf);
            w.write_all(self.data_without_crc)?;
            crc.update(self.data_without_crc);
            let crc = crc.finalize().to_be_bytes();
            w.write_all(&crc)?;
            Ok(buf.len() + self.data_without_crc.len() + 4)
        } else {
            Ok(buf.len())
        }
    }
}

#[derive(Debug, PartialEq)]
pub struct TableSyntaxSection<'a> {
    pub table_id_extension: u16,
    pub data: &'a [u8],
}

impl<'a> TableSyntaxSection<'a> {
    pub fn decode_without_crc(buf: &'a [u8]) -> Result<Self, DecodeError> {
        if buf.len() < 9 {
            return Err(DecodeError::new("not enough bytes for table syntax section"));
        }
        Ok(Self {
            table_id_extension: (buf[0] as u16) << 8 | buf[1] as u16,
            data: &buf[5..buf.len()],
        })
    }

    pub fn encode_without_crc<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        let mut buf = [0u8; 5];
        buf[0] = (self.table_id_extension >> 8) as _;
        buf[1] = self.table_id_extension as _;
        buf[2] = 0b11000001;
        w.write_all(&buf)?;
        w.write_all(self.data)?;
        Ok(buf.len() + self.data.len())
    }
}

#[derive(Debug, PartialEq)]
pub struct PATEntry {
    pub program_number: u16,
    pub program_map_pid: u16,
}

impl PATEntry {
    pub fn decode(buf: &[u8]) -> Result<Self, DecodeError> {
        if buf.len() != 4 {
            return Err(DecodeError::new("unexpected number of bytes for pat entry"));
        }
        Ok(Self {
            program_number: (buf[0] as u16) << 8 | buf[1] as u16,
            program_map_pid: ((buf[2] & 0x1f) as u16) << 8 | buf[3] as u16,
        })
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        w.write_all(&[
            (self.program_number >> 8) as _,
            self.program_number as _,
            0b11100000 | (self.program_map_pid >> 8) as u8,
            self.program_map_pid as _,
        ])?;
        Ok(4)
    }
}

#[derive(Debug, PartialEq)]
pub struct PATData {
    pub entries: Vec<PATEntry>,
}

impl PATData {
    pub fn decode(buf: &[u8]) -> Result<Self, DecodeError> {
        Ok(Self {
            entries: buf.chunks(4).map(PATEntry::decode).collect::<Result<Vec<PATEntry>, DecodeError>>()?,
        })
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        let mut ret = 0;
        for e in &self.entries {
            ret += e.encode(&mut w)?;
        }
        Ok(ret)
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
    pub fn decode(buf: &'a [u8]) -> Result<Self, DecodeError> {
        if buf.len() < PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH {
            return Err(DecodeError::new("not enough bytes for pmt elementary stream info header"));
        }
        let length = ((buf[3] & 0x03) as usize) << 8 | buf[4] as usize;
        if buf.len() < PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH + length {
            return Err(DecodeError::new("not enough bytes for pmt elementary stream info"));
        }
        Ok(Self {
            stream_type: buf[0],
            elementary_pid: ((buf[1] & 0x1f) as u16) << 8 | buf[2] as u16,
            data: &buf[PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH..PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH + length],
        })
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        w.write_all(&[
            self.stream_type,
            0b11100000 | (self.elementary_pid >> 8) as u8,
            self.elementary_pid as _,
            0b11110000 | (self.data.len() >> 8) as u8,
            self.data.len() as _,
        ])?;
        w.write_all(self.data)?;
        Ok(PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH + self.data.len())
    }
}

#[derive(Debug, PartialEq)]
pub struct PMTData<'a> {
    pub pcr_pid: u16,
    pub elementary_stream_info: Vec<PMTElementaryStreamInfo<'a>>,
}

const PMT_HEADER_LENGTH: usize = 4;

impl<'a> PMTData<'a> {
    pub fn decode(buf: &'a [u8]) -> Result<Self, DecodeError> {
        if buf.len() < PMT_HEADER_LENGTH {
            return Err(DecodeError::new("not enough bytes for pmt header"));
        }
        let descs_length = ((buf[2] & 0x03) as usize) << 8 | buf[3] as usize;
        if buf.len() < PMT_HEADER_LENGTH + descs_length {
            return Err(DecodeError::new("not enough bytes for pmt program descriptors"));
        }
        Ok(Self {
            pcr_pid: ((buf[0] & 0x1f) as u16) << 8 | buf[1] as u16,
            elementary_stream_info: {
                let mut infos = Vec::new();
                let mut buf = &buf[PMT_HEADER_LENGTH + descs_length..];
                while !buf.is_empty() {
                    let info = PMTElementaryStreamInfo::decode(buf)?;
                    buf = &buf[PMT_ELEMENTARY_STREAM_INFO_HEADER_LENGTH + info.data.len()..];
                    infos.push(info);
                }
                infos
            },
        })
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        w.write_all(&[0b11100000 | (self.pcr_pid >> 8) as u8, self.pcr_pid as _, 0xf0, 0])?;
        let mut ret = PMT_HEADER_LENGTH;
        for info in &self.elementary_stream_info {
            ret += info.encode(&mut w)?;
        }
        Ok(ret)
    }
}

const PACKET_HEADER_LENGTH: usize = 4;
pub const PACKET_LENGTH: usize = 188;

impl<'a> Packet<'a> {
    pub fn decode(buf: &'a [u8]) -> Result<Self, DecodeError> {
        if buf.len() != PACKET_LENGTH {
            return Err(DecodeError::new("incorrect packet length"));
        }

        if buf[0] != 0x47 {
            return Err(DecodeError::new("incorrect sync byte"));
        }

        let packet_id = ((buf[1] & 0x1f) as u16) << 8 | buf[2] as u16;

        let adaptation_field_control = buf[3] & 0x30;

        let (adaptation_field, adaptation_field_length) = if adaptation_field_control & 0x20 != 0 {
            (
                Some(AdaptationField::decode(&buf[PACKET_HEADER_LENGTH..])?),
                buf[PACKET_HEADER_LENGTH] as usize + 1,
            )
        } else {
            (None, 0)
        };

        let payload = if adaptation_field_control & 0x10 != 0 {
            Some(&buf[PACKET_HEADER_LENGTH + adaptation_field_length..])
        } else {
            None
        };

        Ok(Self {
            packet_id,
            payload_unit_start_indicator: buf[1] & 0x40 != 0,
            continuity_counter: buf[3] & 0x0f,
            adaptation_field,
            payload: payload.map(Cow::Borrowed),
        })
    }

    pub fn decode_table_sections(&self) -> Result<Vec<TableSection<'_>>, DecodeError> {
        let payload = match &self.payload {
            Some(p) => p,
            None => return Ok(vec![]),
        };

        if !self.payload_unit_start_indicator || payload.is_empty() {
            return Ok(vec![]);
        }

        let padding = payload[0] as usize;
        if 1 + padding > payload.len() {
            return Err(DecodeError::new("padding too long for payload"));
        }

        let mut ret = Vec::new();

        let mut buf = &payload[1 + padding..];
        while !buf.is_empty() && buf[0] != 0xff {
            let section = TableSection::decode(buf)?;
            buf = &buf[section.encoded_len()..];
            ret.push(section);
        }

        Ok(ret)
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        let mut buf = [0u8; PACKET_HEADER_LENGTH];
        buf[0] = 0x47;

        if self.payload_unit_start_indicator {
            buf[1] |= 0x40;
        }

        buf[1] |= (self.packet_id >> 8) as u8;
        buf[2] = self.packet_id as _;
        buf[3] = self.continuity_counter as _;

        if self.adaptation_field.is_some() {
            buf[3] |= 0b00100000;
        }

        if self.payload.is_some() {
            buf[3] |= 0b00010000;
        }

        let mut ret = PACKET_HEADER_LENGTH;
        w.write_all(&buf)?;

        if let Some(af) = &self.adaptation_field {
            ret += af.encode(&mut w, PACKET_LENGTH - ret - self.payload.as_ref().map(|p| p.len()).unwrap_or(0))?;
        }

        if let Some(payload) = &self.payload {
            w.write_all(payload)?;
            ret += payload.len();
        }

        if ret != PACKET_LENGTH {
            Err(EncodeError::other("invalid data length"))
        } else {
            Ok(ret)
        }
    }

    /// Returns the maximum possible data length for a packet with the given adaptation field.
    pub fn max_payload_len(af: Option<&AdaptationField>) -> usize {
        PACKET_LENGTH - PACKET_HEADER_LENGTH - af.map(|af| af.encoded_len()).unwrap_or(0)
    }
}

pub fn decode_packets(buf: &[u8]) -> Result<Vec<Packet>, DecodeError> {
    buf.chunks(PACKET_LENGTH).map(Packet::decode).collect()
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::temi::TimeFieldLength;
    use std::{fs::File, io::Read};

    #[test]
    fn test_decode_encode() {
        let mut f = File::open("src/testdata/pro-bowl.ts").unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();
        let packets = decode_packets(&buf).unwrap();

        assert_eq!(packets.len(), 47128);

        assert_eq!(
            packets[3].adaptation_field,
            Some(AdaptationField {
                discontinuity_indicator: Some(false),
                random_access_indicator: Some(true),
                program_clock_reference_27mhz: Some(18_900_000),
                private_data_types: vec![],
                temi_timeline_descriptors: vec![],
            })
        );
        {
            let mut encoded = vec![];
            packets[3].adaptation_field.as_ref().unwrap().encode(&mut encoded, 0).unwrap();
            assert_eq!(buf[188 * 3 + 4..188 * 3 + 12], encoded);
            assert_eq!(packets[3].adaptation_field.as_ref().unwrap().encoded_len(), encoded.len());
        }

        let mut last_pcr = 0;
        let mut rais = 0;
        for (i, p) in packets.iter().enumerate() {
            {
                let mut encoded = vec![];
                p.encode(&mut encoded).unwrap();
                assert_eq!(buf[188 * i..188 * (i + 1)], encoded);
            }

            // PAT
            if p.packet_id == PID_PAT {
                let table_sections = p.decode_table_sections().unwrap();
                assert_eq!(table_sections.len(), 1);
                assert_eq!(table_sections[0].table_id, TABLE_ID_PAT);
                {
                    let mut encoded = vec![];
                    table_sections[0].encode(&mut encoded).unwrap();
                    assert_eq!(p.payload.as_ref().unwrap()[1..1 + table_sections[0].encoded_len()], encoded);
                }

                let syntax_section = table_sections[0].decode_syntax_section().unwrap();
                assert_eq!(syntax_section.table_id_extension, 1);
                {
                    let mut encoded = vec![];
                    syntax_section.encode_without_crc(&mut encoded).unwrap();
                    assert_eq!(table_sections[0].data_without_crc, encoded);
                }

                let pat = PATData::decode(syntax_section.data).unwrap();
                {
                    let mut encoded = vec![];
                    pat.encode(&mut encoded).unwrap();
                    assert_eq!(syntax_section.data, encoded);
                }

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
                {
                    let mut encoded = vec![];
                    table_sections[0].encode(&mut encoded).unwrap();
                    assert_eq!(p.payload.as_ref().unwrap()[1..1 + table_sections[0].encoded_len()], encoded);
                }

                let syntax_section = table_sections[0].decode_syntax_section().unwrap();
                assert_eq!(syntax_section.table_id_extension, 1);
                {
                    let mut encoded = vec![];
                    syntax_section.encode_without_crc(&mut encoded).unwrap();
                    assert_eq!(table_sections[0].data_without_crc, encoded);
                }

                let pmt = PMTData::decode(syntax_section.data).unwrap();
                {
                    let mut encoded = vec![];
                    pmt.encode(&mut encoded).unwrap();
                    assert_eq!(syntax_section.data, encoded);
                }

                assert_eq!(pmt.elementary_stream_info.len(), 2);
            }

            if let Some(f) = &p.adaptation_field {
                if f.random_access_indicator.unwrap_or(false) {
                    rais += 1;
                }
                if let Some(pcr) = f.program_clock_reference_27mhz {
                    assert!(pcr > last_pcr);
                    last_pcr = pcr;
                }
            }
        }
        assert_eq!(rais, 62);
        assert_eq!(last_pcr, 286_917_900);
    }

    #[test]
    fn test_empty_adaptation_field() {
        let data = vec![
            0x47, 0x40, 0x21, 0x38, 0x00, 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x85, 0x80, 0x07, 0x23, 0x5F, 0xFF, 0x4F, 0x8B, 0xFF, 0xFF, 0x00, 0x00, 0x00,
            0x01, 0x09, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x09, 0x1A, 0x24, 0x17, 0x1C, 0x1A, 0x00, 0x00, 0x03, 0x00, 0x40, 0x80, 0x00,
            0x00, 0x00, 0x01, 0x21, 0x9A, 0x00, 0x07, 0x03, 0x01, 0xE0, 0x05, 0x42, 0xE8, 0x4B, 0x90, 0x12, 0x5C, 0xE7, 0x5F, 0xD2, 0x97, 0x9E, 0xAD, 0x7F,
            0x08, 0xF4, 0x31, 0xCB, 0xE6, 0x1B, 0xD8, 0x20, 0x67, 0xCD, 0xEA, 0x14, 0xE5, 0xEB, 0x2F, 0x2F, 0x40, 0xA1, 0xDE, 0x43, 0x68, 0xA1, 0xBE, 0xAD,
            0xF5, 0x6F, 0xAB, 0x90, 0x65, 0xD5, 0xC8, 0x9E, 0xAD, 0x00, 0x9B, 0x75, 0x72, 0x6E, 0xBD, 0x02, 0xFF, 0x56, 0x80, 0x10, 0x8B, 0xAB, 0xC3, 0x5D,
            0x5D, 0x27, 0x56, 0x20, 0x6C, 0xEB, 0x2A, 0xEA, 0xF0, 0x16, 0x3D, 0x5E, 0x0A, 0x7A, 0xE5, 0x00, 0xA7, 0xF4, 0x66, 0x81, 0x83, 0xAB, 0x97, 0xC8,
            0x7A, 0x00, 0xFE, 0xAE, 0x5F, 0x58, 0x21, 0x0E, 0xAC, 0x17, 0x56, 0x01, 0x1E, 0x8C, 0xC4, 0x25, 0xD1, 0xDC, 0x1F, 0xA3, 0xBD, 0x72, 0x9A, 0xC0,
            0x79, 0xBB, 0xA2, 0xE5, 0xEA, 0xC4, 0xBD, 0x1D, 0xC9, 0x3A, 0xF4, 0x57, 0x7D, 0x80, 0x9A, 0x11, 0xD5, 0xFE, 0xBD, 0x2C,
        ];

        let packet = Packet::decode(&data).unwrap();
        assert_eq!(packet.adaptation_field, Some(AdaptationField::default()));
        assert_eq!(packet.payload.unwrap(), &data[5..]);
    }

    #[test]
    fn test_af_temi_timeline_descriptor() {
        let mut f = File::open("src/testdata/pro-bowl.ts").unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();
        let packets = decode_packets(&buf).unwrap();
        let mut packet = packets.into_iter().find(|p| p.packet_id == 0x0100).unwrap();

        let af = packet.adaptation_field.as_mut().unwrap();
        let temi1 = TEMITimelineDescriptor {
            has_timestamp: TimeFieldLength::None,
            timescale: 0,
            media_timestamp: 0,
            ntp_timestamp: Some(1652398146422),
            ptp_timestamp: Some(0xcdf8_fdc9_b5f8_25e9_9236),
            has_timecode: TimeFieldLength::None,
            drop: false,
            frames_per_tc_seconds: 0,
            duration: 0,
            time_code: 0,
            force_reload: true,
            paused: true,
            discontinuity: true,
            timeline_id: 0xa1,
        };

        let temi2 = TEMITimelineDescriptor {
            has_timestamp: TimeFieldLength::Long,
            timescale: 225,
            media_timestamp: 8232432,

            ntp_timestamp: Some(0xfdc9_b5f8_25e9_9236),
            ptp_timestamp: None,

            has_timecode: TimeFieldLength::Short,
            drop: true,
            frames_per_tc_seconds: 0x35a3,
            duration: 0x9a8a,
            time_code: 0xfb_82ef,

            force_reload: true,
            paused: true,
            discontinuity: false,
            timeline_id: 0xf3,
        };

        let payload = &packet.payload.unwrap()[temi1.encoded_len() + temi2.encoded_len() + 2..];
        af.temi_timeline_descriptors = vec![temi1, temi2];
        packet.payload = Some(Cow::Borrowed(payload));

        let mut w = vec![];
        packet.encode(&mut w).unwrap();

        let decoded_packet = Packet::decode(&w).unwrap();
        assert_eq!(decoded_packet, packet);
    }

    #[test]
    fn test_decode_empty_temi_descriptor() {
        let packet_data = vec![
            71, 72, 53, 52, 20, 1, 18, 15, 4, 15, 129, 127, 200, 0, 0, 3, 232, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 224, 19, 148, 143, 192, 10, 49, 126, 133, 202,
            33, 17, 126, 133, 145, 225, 0, 0, 0, 1, 9, 80, 0, 0, 1, 6, 1, 1, 50, 4, 10, 181, 0, 49, 68, 84, 71, 49, 65, 254, 255, 128, 0, 0, 0, 1, 65, 158,
            188, 61, 47, 16, 83, 40, 159, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3,
            0, 0, 3, 0, 0, 3, 0, 0, 3, 0, 0, 3, 2, 26, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 12, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        ];
        let packet = Packet::decode(&packet_data).unwrap();
        let af = packet.adaptation_field.unwrap();
        let temi_descriptors = af.temi_timeline_descriptors;
        assert_eq!(temi_descriptors.len(), 1);
        let decoded_temi = &temi_descriptors[0];
        let temi = TEMITimelineDescriptor {
            has_timestamp: TimeFieldLength::Long,
            timescale: 1000,
            paused: true,
            timeline_id: 200,
            ..TEMITimelineDescriptor::default()
        };
        assert_eq!(decoded_temi, &temi);
    }

    #[test]
    fn test_decode_af_private_data_type() {
        let ori_packet_data = vec![
            71, 66, 89, 60, 34, 3, 13, 2, 11, 19, 14, 17, 173, 40, 201, 3, 4, 0, 8, 0, 18, 15, 4, 15, 129, 127, 206, 0, 15, 66, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 1, 224, 52, 128, 139, 128, 5, 33, 70, 181, 81, 147, 0, 0, 1, 0, 1, 223, 255, 251, 184, 0, 0, 1, 181, 131, 51, 51, 156, 0, 0, 0, 1, 1, 34, 164,
            64, 193, 88, 0, 0, 1, 2, 34, 171, 0, 194, 176, 0, 0, 1, 3, 34, 171, 0, 194, 176, 0, 0, 1, 4, 34, 171, 0, 194, 176, 0, 0, 1, 5, 34, 171, 0, 194,
            176, 0, 0, 1, 6, 34, 171, 0, 194, 176, 0, 0, 1, 7, 34, 171, 0, 194, 176, 0, 0, 1, 8, 34, 171, 0, 194, 176, 0, 0, 1, 9, 34, 171, 0, 194, 176, 0, 0,
            1, 10, 34, 171, 0, 194, 176, 0, 0, 1, 11, 34, 171, 0, 194, 176, 0, 0, 1, 12, 34, 171, 0, 194, 176, 0, 0, 1, 13, 34, 171, 0, 194, 176,
        ];
        let decoded_packet = Packet::decode(&ori_packet_data).unwrap();
        let af = decoded_packet.adaptation_field.as_ref().unwrap();
        let temi_descriptors = &af.temi_timeline_descriptors;
        assert_eq!(temi_descriptors.len(), 1);
        let decoded_temi = &temi_descriptors[0];
        let temi = TEMITimelineDescriptor {
            has_timestamp: TimeFieldLength::Long,
            timescale: 1000000,
            paused: true,
            timeline_id: 206,
            ..TEMITimelineDescriptor::default()
        };
        assert_eq!(decoded_temi, &temi);
        assert_eq!(&af.private_data_types, &[2, 11, 19, 14, 17, 173, 40, 201, 3, 4, 0, 8, 0]);
    }

    #[test]
    fn test_decode_video_with_temi_timelines() {
        let mut f = File::open("src/testdata/uk_psb1_temi.ts").unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();
        decode_packets(&buf).unwrap();
    }
}
