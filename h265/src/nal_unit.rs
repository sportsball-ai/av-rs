use super::{decode, syntax_elements::*, Bitstream, Decode};
pub use h264::RBSP;
use std::io;

pub const NAL_UNIT_TYPE_VIDEO_PARAMETER_SET: u8 = 32;
pub const NAL_UNIT_TYPE_SEQUENCE_PARAMETER_SET: u8 = 33;

// ITU-T H.265, 11/2019, 7.3.1.1
pub struct NALUnit<T> {
    pub nal_unit_header: NALUnitHeader,
    pub rbsp_byte: RBSP<T>,
}

impl<'a, T: Iterator<Item = &'a u8>> NALUnit<T> {
    pub fn decode(mut bs: Bitstream<T>) -> io::Result<Self> {
        Ok(Self {
            nal_unit_header: NALUnitHeader::decode(&mut bs)?,
            rbsp_byte: RBSP::new(bs.into_inner()),
        })
    }
}

// ITU-T H.265, 11/2019, 7.3.1.1
#[derive(Default)]
pub struct NALUnitHeader {
    pub forbidden_zero_bit: F1,
    pub nal_unit_type: U6,
    pub nuh_layer_id: U6,
    pub nuh_temporal_id_plus1: U3,
}

impl Decode for NALUnitHeader {
    fn decode<'a, T: Iterator<Item = &'a u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(
            bs,
            &mut ret.forbidden_zero_bit,
            &mut ret.nal_unit_type,
            &mut ret.nuh_layer_id,
            &mut ret.nuh_temporal_id_plus1
        )?;

        if ret.nal_unit_type.0 < 48 && ret.forbidden_zero_bit.0 != 0 {
            return Err(io::Error::new(io::ErrorKind::Other, "non-zero forbidden_zero_bit"));
        }

        Ok(ret)
    }
}
