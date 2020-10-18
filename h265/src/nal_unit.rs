use super::{decode, encode, syntax_elements::*, Bitstream, BitstreamWriter, Decode, Encode};
pub use h264::{EmulationPrevention, RBSP};
use std::io;

pub const NAL_UNIT_TYPE_TRAIL_N: u8 = 0;
pub const NAL_UNIT_TYPE_TRAIL_R: u8 = 1;
pub const NAL_UNIT_TYPE_TSA_N: u8 = 2;
pub const NAL_UNIT_TYPE_TSA_R: u8 = 3;
pub const NAL_UNIT_TYPE_STSA_N: u8 = 4;
pub const NAL_UNIT_TYPE_STSA_R: u8 = 5;
pub const NAL_UNIT_TYPE_RADL_N: u8 = 6;
pub const NAL_UNIT_TYPE_RADL_R: u8 = 7;
pub const NAL_UNIT_TYPE_RASL_N: u8 = 8;
pub const NAL_UNIT_TYPE_RASL_R: u8 = 9;
pub const NAL_UNIT_TYPE_BLA_W_LP: u8 = 16;
pub const NAL_UNIT_TYPE_BLA_W_RADL: u8 = 17;
pub const NAL_UNIT_TYPE_BLA_N_LP: u8 = 18;
pub const NAL_UNIT_TYPE_IDR_W_RADL: u8 = 19;
pub const NAL_UNIT_TYPE_IDR_N_LP: u8 = 20;
pub const NAL_UNIT_TYPE_CRA_NUT: u8 = 21;
pub const NAL_UNIT_TYPE_RSV_IRAP_VCL22: u8 = 22;
pub const NAL_UNIT_TYPE_RSV_IRAP_VCL23: u8 = 23;
pub const NAL_UNIT_TYPE_VPS_NUT: u8 = 32;
pub const NAL_UNIT_TYPE_SPS_NUT: u8 = 33;
pub const NAL_UNIT_TYPE_PPS_NUT: u8 = 34;

// ITU-T H.265, 11/2019, 7.3.1.1
pub struct NALUnit<RBSP> {
    pub nal_unit_header: NALUnitHeader,

    // rbsp_byte does not include emulation prevention bytes.
    pub rbsp_byte: RBSP,
}

impl<RBSP: Clone> Clone for NALUnit<RBSP> {
    fn clone(&self) -> Self {
        Self {
            nal_unit_header: self.nal_unit_header.clone(),
            rbsp_byte: self.rbsp_byte.clone(),
        }
    }
}

impl<T: Iterator<Item = u8>> NALUnit<RBSP<T>> {
    pub fn decode(mut bs: Bitstream<T>) -> io::Result<Self> {
        Ok(Self {
            nal_unit_header: NALUnitHeader::decode(&mut bs)?,
            rbsp_byte: RBSP::new(bs.into_inner()),
        })
    }
}

impl<RBSP: IntoIterator<Item = u8>> NALUnit<RBSP> {
    pub fn encode<T: io::Write>(self, bs: &mut BitstreamWriter<T>) -> io::Result<()> {
        self.nal_unit_header.encode(bs)?;
        bs.flush()?;
        bs.inner_mut()
            .write_all(&EmulationPrevention::new(self.rbsp_byte.into_iter()).collect::<Vec<u8>>())
    }
}

// ITU-T H.265, 11/2019, 7.3.1.1
#[derive(Clone, Default)]
pub struct NALUnitHeader {
    pub forbidden_zero_bit: F1,
    pub nal_unit_type: U6,
    pub nuh_layer_id: U6,
    pub nuh_temporal_id_plus1: U3,
}

impl Decode for NALUnitHeader {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
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

impl Encode for NALUnitHeader {
    fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>) -> io::Result<()> {
        encode!(
            bs,
            &self.forbidden_zero_bit,
            &self.nal_unit_type,
            &self.nuh_layer_id,
            &self.nuh_temporal_id_plus1
        )
    }
}
