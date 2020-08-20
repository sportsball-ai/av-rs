use super::{decode, syntax_elements::*, Bitstream, Decode};
use std::io;

#[derive(Debug, Default)]
pub struct SliceSegmentHeader {
    pub first_slice_segment_in_pic_flag: U1,
}

impl Decode for SliceSegmentHeader {
    fn decode<'a, T: Iterator<Item = &'a u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(bs, &mut ret.first_slice_segment_in_pic_flag)?;

        Ok(ret)
    }
}
