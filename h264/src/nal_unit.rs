use super::{decode, syntax_elements::*, Bitstream, Decode};

use std::io;

/// The first byte of each NALU contains its type. If you just need the type without decoding the
/// NALU, mask the first byte with this.
pub const NAL_UNIT_TYPE_MASK: u8 = 0x1f;

pub const NAL_UNIT_TYPE_SEQUENCE_PARAMETER_SET: u8 = 7;
pub const NAL_UNIT_TYPE_PICTURE_PARAMETER_SET: u8 = 8;

// ITU-T H.264, 04/2017, 7.3.1
#[derive(Default)]
pub struct NALUnit {
    pub forbidden_zero_bit: F1,
    pub nal_ref_idc: U2,
    pub nal_unit_type: U5,
    pub rbsp_byte: Vec<u8>,
}

pub fn decode_rbsp<T: AsRef<[u8]>>(bs: &mut Bitstream<T>) -> io::Result<Vec<u8>> {
    let mut rbsp = Vec::with_capacity(bs.bits_remaining() / 8);
    while bs.bits_remaining() >= 8 {
        if bs.next_bits(24) == Some(0x000003) {
            rbsp.push(0);
            rbsp.push(0);
            bs.advance_bits(24);
        } else {
            rbsp.push(bs.read_bits(8)? as u8);
        }
    }
    Ok(rbsp)
}

impl Decode for NALUnit {
    fn decode<T: AsRef<[u8]>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(bs, &mut ret.forbidden_zero_bit, &mut ret.nal_ref_idc, &mut ret.nal_unit_type)?;

        if ret.forbidden_zero_bit.0 != 0 {
            return Err(io::Error::new(io::ErrorKind::Other, "non-zero forbidden_zero_bit"));
        }

        match ret.nal_unit_type.0 {
            14 | 20 | 21 => return Err(io::Error::new(io::ErrorKind::Other, "unsupported nal_unit_type")),
            _ => {}
        }

        ret.rbsp_byte = decode_rbsp(bs)?;

        Ok(ret)
    }
}
