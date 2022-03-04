#![no_std]

#[macro_use]
extern crate alloc;

use alloc::vec::Vec;
use byteorder::{BigEndian, ReadBytesExt};

pub use core2::io;
use core2::io::Read;

#[derive(Clone, Debug, PartialEq)]
pub struct AVCDecoderConfigurationRecord {
    pub configuration_version: u8,
    pub avc_profile_indication: u8,
    pub profile_compatibility: u8,
    pub avc_level_indication: u8,
    pub length_size_minus_one: u8,
    pub sequence_parameter_sets: Vec<Vec<u8>>,
    pub picture_parameter_sets: Vec<Vec<u8>>,
}

impl AVCDecoderConfigurationRecord {
    pub fn decode<R: Read>(mut r: R) -> io::Result<Self> {
        Ok(Self {
            configuration_version: match r.read_u8()? {
                1 => 1,
                v => return Err(io::Error::new(io::ErrorKind::Other, format!("unexpected configuration version: {}", v))),
            },
            avc_profile_indication: r.read_u8()?,
            profile_compatibility: r.read_u8()?,
            avc_level_indication: r.read_u8()?,
            length_size_minus_one: r.read_u8()? & 0x3,
            sequence_parameter_sets: {
                let count = r.read_u8()? & 0x1f;
                let mut sets = Vec::with_capacity(count as _);
                for _ in 0..count {
                    let len = r.read_u16::<BigEndian>()? as usize;
                    let mut sps = vec![0; len];
                    r.read_exact(&mut sps)?;
                    sets.push(sps);
                }
                sets
            },
            picture_parameter_sets: {
                let count = r.read_u8()?;
                let mut sets = Vec::with_capacity(count as _);
                for _ in 0..count {
                    let len = r.read_u16::<BigEndian>()? as usize;
                    let mut pps = vec![0; len];
                    r.read_exact(&mut pps)?;
                    sets.push(pps);
                }
                sets
            },
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct HVCDecoderConfigurationRecord {
    pub configuration_version: u8,
    pub general_profile_space: u8,
    pub general_tier_flag: bool,
    pub general_profile_idc: u8,
    pub general_profile_compatibility_flags: u32,
    pub general_constraint_indicator_flags: u64,
    pub general_level_idc: u8,
    pub min_spatial_segmentation_idc: u16,
    pub parallelism_type: u8,
    pub chroma_format: u8,
    pub bit_depth_luma_minus_eight: u8,
    pub bit_depth_chroma_minus_eight: u8,
    pub average_frame_rate: u16,
    pub constant_frame_rate: u8,
    pub num_temporal_layers: u8,
    pub temporal_id_nested: u8,
    pub length_size_minus_one: u8,
    pub nal_units: Vec<Vec<u8>>,
}

impl HVCDecoderConfigurationRecord {
    pub fn decode<R: Read>(mut r: R) -> io::Result<Self> {
        let configuration_version = match r.read_u8()? {
            1 => 1,
            v => return Err(io::Error::new(io::ErrorKind::Other, format!("unexpected configuration version: {}", v))),
        };
        let b = r.read_u8()?;
        let general_profile_space = b >> 6;
        let general_tier_flag = ((b >> 5) & 1) == 1;
        let general_profile_idc = b & 0x1f;
        let general_profile_compatibility_flags = r.read_u32::<BigEndian>()?;
        let general_constraint_indicator_flags = ((r.read_u32::<BigEndian>()? as u64) << 16) | r.read_u16::<BigEndian>()? as u64;
        let general_level_idc = r.read_u8()?;
        let min_spatial_segmentation_idc = r.read_u16::<BigEndian>()? & 0xfff;
        let parallelism_type = r.read_u8()? & 3;
        let chroma_format = r.read_u8()? & 3;
        let bit_depth_luma_minus_eight = r.read_u8()? & 3;
        let bit_depth_chroma_minus_eight = r.read_u8()? & 3;
        let average_frame_rate = r.read_u16::<BigEndian>()?;
        let b = r.read_u8()?;
        let constant_frame_rate = b >> 6;
        let num_temporal_layers = (b >> 3) & 7;
        let temporal_id_nested = (b >> 2) & 1;
        let length_size_minus_one = b & 3;
        Ok(Self {
            configuration_version,
            general_profile_space,
            general_tier_flag,
            general_profile_idc,
            general_profile_compatibility_flags,
            general_constraint_indicator_flags,
            general_level_idc,
            min_spatial_segmentation_idc,
            parallelism_type,
            chroma_format,
            bit_depth_luma_minus_eight,
            bit_depth_chroma_minus_eight,
            average_frame_rate,
            constant_frame_rate,
            num_temporal_layers,
            temporal_id_nested,
            length_size_minus_one,
            nal_units: {
                let count = r.read_u8()?;
                let mut nalus = vec![];
                for _ in 0..count {
                    r.read_u8()?;
                    let nalu_count = r.read_u16::<BigEndian>()?;
                    for _ in 0..nalu_count {
                        let len = r.read_u16::<BigEndian>()? as usize;
                        let mut data = vec![0; len];
                        r.read_exact(&mut data)?;
                        nalus.push(data);
                    }
                }
                nalus
            },
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct AudioSpecificConfig {
    pub object_type: u16,
}

impl AudioSpecificConfig {
    pub fn decode<R: Read>(mut r: R) -> io::Result<Self> {
        let b = r.read_u16::<BigEndian>()?;
        Ok(Self {
            object_type: if b & 0xf800 == 0xf800 { ((b & 0x7e0) >> 5) + 32 } else { (b & 0xf800) >> 11 },
        })
    }
}

pub const ES_DESCR_TAG: u8 = 0x03;
pub const DECODER_CONFIG_DESCR_TAG: u8 = 0x04;
pub const DEC_SPECIFIC_INFO_TAG: u8 = 0x05;

#[derive(Debug)]
pub struct Descriptor<'a> {
    pub tag: u8,
    pub data: &'a [u8],
}

impl<'a> Descriptor<'a> {
    pub fn parse(b: &'a [u8]) -> io::Result<(Self, usize)> {
        if b.is_empty() {
            return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "expected tag, found eof"));
        }
        let tag = b[0];
        let mut offset = 1;
        let mut len: usize = 0;
        for _ in 0..4 {
            if offset >= b.len() {
                return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "expected len, found eof"));
            }
            let c = b[offset];
            offset += 1;
            len = (len << 7) | (c & 0x7f) as usize;
            if (c & 0x80) == 0 {
                break;
            }
        }
        if offset + len > b.len() {
            return Err(io::Error::new(io::ErrorKind::UnexpectedEof, format!("not enough data for len {}", len)));
        }
        Ok((
            Self {
                tag,
                data: &b[offset..offset + len],
            },
            offset + len,
        ))
    }
}

#[derive(Debug)]
pub struct ESDescriptorData<'a> {
    pub decoder_config_descriptor: Descriptor<'a>,
}

impl<'a> ESDescriptorData<'a> {
    pub fn parse(b: &'a [u8]) -> io::Result<Self> {
        if b.len() < 3 {
            return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "expected flags, found eof"));
        }

        let flags = b[2];
        let mut offset = 3;
        if (flags & 0x80) != 0 {
            // skip over the dependency id
            offset += 2;
        }
        if (flags & 0x40) != 0 {
            // skip over the url string
            if b.len() <= offset {
                return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "expected url string len, found eof"));
            }
            offset += 1 + b[offset] as usize;
        }
        if (flags & 0x20) != 0 {
            // skip over ocr stream id
            offset += 2;
        }
        if b.len() <= offset {
            return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "expected decoder config, found eof"));
        }
        Ok(Self {
            decoder_config_descriptor: {
                let next = Descriptor::parse(&b[offset..])?.0;
                if next.tag != DECODER_CONFIG_DESCR_TAG {
                    return Err(io::Error::new(
                        io::ErrorKind::UnexpectedEof,
                        format!("expected decoder config, found descriptor with tag {}", next.tag),
                    ));
                }
                next
            },
        })
    }
}

#[derive(Debug)]
pub struct DecoderConfigDescriptorData<'a> {
    pub object_type_indication: u8,
    pub decoder_specific_info_descriptor: Option<Descriptor<'a>>,
}

impl<'a> DecoderConfigDescriptorData<'a> {
    pub fn parse(b: &'a [u8]) -> io::Result<Self> {
        if b.len() < 13 {
            return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "not enough bytes for decoder config"));
        }
        Ok(Self {
            object_type_indication: b[0],
            decoder_specific_info_descriptor: {
                let b = &b[13..];
                if b.is_empty() {
                    None
                } else {
                    let next = Descriptor::parse(b)?.0;
                    if next.tag == DEC_SPECIFIC_INFO_TAG {
                        Some(next)
                    } else {
                        None
                    }
                }
            },
        })
    }
}

#[derive(Debug, PartialEq)]
pub enum AudioDataTransportStreamMPEGVersion {
    MPEG4,
    MPEG2,
}

#[derive(Debug)]
pub struct AudioDataTransportStream<'a> {
    pub mpeg_version: AudioDataTransportStreamMPEGVersion,
    pub mpeg4_audio_object_type: u32,
    pub channel_count: u32,
    pub sample_rate: u32,
    pub frame_length: usize,
    pub aac_data: &'a [u8],
}

impl<'a> AudioDataTransportStream<'a> {
    pub fn parse(b: &'a [u8]) -> io::Result<Self> {
        if b[0] != 0xff || (b[1] & 0xf0) != 0xf0 {
            return Err(io::Error::new(io::ErrorKind::Other, "invalid adts syncword"));
        }
        let len = (((b[3] & 3) as usize) << 11) | ((b[4] as usize) << 3) | ((b[5] as usize) >> 5);
        if len < 7 || len > b.len() {
            return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "invalid adts frame length"));
        }
        let mpeg_version = if (b[1] & 0x08) == 0 {
            AudioDataTransportStreamMPEGVersion::MPEG4
        } else {
            AudioDataTransportStreamMPEGVersion::MPEG2
        };
        let has_crc = (b[1] & 1) == 0;
        let mpeg4_audio_object_type = (b[2] >> 6) as u32 + 1;
        let channel_count = match ((b[2] & 1) << 2) | (b[3] >> 6) {
            7 => 8,
            c => c as _,
        };
        let sample_rate = match (b[2] >> 2) & 0x0f {
            0 => 96_000,
            1 => 88_200,
            2 => 64_000,
            3 => 48_000,
            4 => 44_100,
            5 => 32_000,
            6 => 24_000,
            7 => 22_050,
            8 => 16_000,
            9 => 12_000,
            10 => 11_025,
            11 => 8_000,
            12 => 7_350,
            _ => 0,
        };
        Ok(Self {
            mpeg_version,
            mpeg4_audio_object_type,
            channel_count,
            sample_rate,
            frame_length: len,
            aac_data: &b[if has_crc { 9 } else { 7 }..len],
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_avc_decoder_configuration_decode() {
        let data = &[
            0x01, 0x4d, 0x40, 0x1f, 0xff, 0xe1, 0x00, 0x1c, 0x67, 0x4d, 0x40, 0x1f, 0xec, 0xa0, 0x28, 0x02, 0xdd, 0x80, 0xb5, 0x01, 0x01, 0x01, 0x40, 0x00,
            0x00, 0x03, 0x00, 0x40, 0x00, 0x05, 0xdc, 0x03, 0xc6, 0x0c, 0x65, 0x80, 0x01, 0x00, 0x04, 0x68, 0xef, 0xbc, 0x80,
        ];

        let record = AVCDecoderConfigurationRecord::decode(data.as_ref()).unwrap();

        assert_eq!(1, record.configuration_version);
        assert_eq!(77, record.avc_profile_indication);
        assert_eq!(0x40, record.profile_compatibility);
        assert_eq!(31, record.avc_level_indication);
        assert_eq!(3, record.length_size_minus_one);

        assert_eq!(
            vec![vec![
                0x67, 0x4d, 0x40, 0x1f, 0xec, 0xa0, 0x28, 0x02, 0xdd, 0x80, 0xb5, 0x01, 0x01, 0x01, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x05, 0xdc, 0x03,
                0xc6, 0x0c, 0x65, 0x80
            ]],
            record.sequence_parameter_sets
        );

        assert_eq!(vec![vec![0x68, 0xef, 0xbc, 0x80]], record.picture_parameter_sets);
    }

    #[test]
    fn test_hvc_decoder_configuration_decode() {
        let data = &[
            0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x99, 0xF0, 0x00, 0xFC, 0xFD, 0xF8, 0xF8, 0x00, 0x00, 0x0F, 0x03, 0xA0,
            0x00, 0x01, 0x00, 0x18, 0x40, 0x01, 0x0C, 0x01, 0xFF, 0xFF, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xB0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
            0x99, 0x17, 0x02, 0x40, 0xA1, 0x00, 0x01, 0x00, 0x25, 0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xB0, 0x00, 0x00, 0x03, 0x00, 0x00,
            0x03, 0x00, 0x99, 0xA0, 0x03, 0xC0, 0x80, 0x11, 0x07, 0xCB, 0x88, 0x17, 0xB9, 0x16, 0x45, 0x2F, 0xFC, 0xB9, 0xFC, 0x4F, 0xE8, 0x80, 0xA2, 0x00,
            0x01, 0x00, 0x07, 0x44, 0x01, 0xC0, 0x72, 0xF0, 0x53, 0x24,
        ];

        let record = HVCDecoderConfigurationRecord::decode(data.as_ref()).unwrap();

        assert_eq!(1, record.configuration_version);
        assert_eq!(3, record.length_size_minus_one);

        assert_eq!(
            vec![
                // vps
                vec![
                    0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x99, 0x17, 0x02,
                    0x40,
                ],
                // sps
                vec![
                    0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x99, 0xa0, 0x03, 0xc0, 0x80, 0x11,
                    0x07, 0xcb, 0x88, 0x17, 0xb9, 0x16, 0x45, 0x2f, 0xfc, 0xb9, 0xfc, 0x4f, 0xe8, 0x80
                ],
                // pps
                vec![0x44, 0x01, 0xc0, 0x72, 0xf0, 0x53, 0x24]
            ],
            record.nal_units
        );
    }

    #[test]
    fn test_audio_specific_config_decode() {
        let data = &[0x11, 0x90];
        assert_eq!(AudioSpecificConfig::decode(data.as_ref()).unwrap(), AudioSpecificConfig { object_type: 2 })
    }

    #[test]
    fn test_descriptor() {
        let data = &[0x05, 0x80, 0x80, 0x80, 0x02, 0x11, 0x90];
        let (desc, n) = Descriptor::parse(data.as_ref()).unwrap();
        assert_eq!(n, 7);
        assert_eq!(desc.tag, DEC_SPECIFIC_INFO_TAG);
        assert_eq!(desc.data.len(), 2);
    }
}
