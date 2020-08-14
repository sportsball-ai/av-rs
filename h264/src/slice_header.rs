use super::{decode, syntax_elements::*, Bitstream, SequenceParameterSet};
use std::io;

#[derive(Debug, Default)]
pub struct SliceHeader {
    pub first_mb_in_slice: UE,
    pub slice_type: UE,
    pub pic_parameter_set_id: UE,

    // if( separate_colour_plane_flag = = 1 )
    pub colour_plane_id: U2,

    pub frame_num: u64,
}

impl SliceHeader {
    pub fn decode<'a, T: Iterator<Item = &'a u8>>(bs: &mut Bitstream<T>, sps: &SequenceParameterSet) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(bs, &mut ret.first_mb_in_slice, &mut ret.slice_type, &mut ret.pic_parameter_set_id)?;

        if sps.separate_colour_plane_flag.0 == 1 {
            decode!(bs, &mut ret.colour_plane_id)?;
        }

        ret.frame_num = bs.read_bits(sps.log2_max_frame_num_minus4.0 as usize + 4)?;

        Ok(ret)
    }
}
