use std::io;

pub mod nal_unit;
pub use nal_unit::*;

pub use h264::decode;

pub use h264::bitstream;
pub use h264::bitstream::*;

pub mod profile_tier_level;
pub use profile_tier_level::*;

pub mod sequence_parameter_set;
pub use sequence_parameter_set::*;

pub mod slice_segment_header;
pub use slice_segment_header::*;

pub mod video_parameter_set;
pub use video_parameter_set::*;

pub mod syntax_elements;
pub use syntax_elements::*;

pub use h264::{iterate_annex_b, iterate_avcc};

#[derive(Clone)]
pub struct AccessUnitCounter {
    maybe_start_new_access_unit: bool,
    count: u64,
}

impl AccessUnitCounter {
    pub fn new() -> Self {
        Self {
            maybe_start_new_access_unit: true,
            count: 0,
        }
    }

    pub fn count(&self) -> u64 {
        self.count
    }

    pub fn count_nalu<T: AsRef<[u8]>>(&mut self, nalu: T) -> io::Result<()> {
        let nalu = nalu.as_ref();

        let mut bs = Bitstream::new(nalu);
        let header = NALUnitHeader::decode(&mut bs)?;

        // ITU-T H.265, 11/2019, 7.4.2.4.4
        match header.nal_unit_type.0 {
            0..=9 | 16..=21 => {
                if self.maybe_start_new_access_unit {
                    let bs = Bitstream::new(nalu);
                    let mut nalu = NALUnit::decode(bs)?;
                    let mut rbsp = Bitstream::new(&mut nalu.rbsp_byte);
                    let slice_segment_header = SliceSegmentHeader::decode(&mut rbsp)?;
                    if slice_segment_header.first_slice_segment_in_pic_flag.0 != 0 {
                        self.count += 1;
                    }
                }
                self.maybe_start_new_access_unit = true;
            }
            0..=31 => self.maybe_start_new_access_unit = true,
            _ => {
                if self.maybe_start_new_access_unit && header.nuh_layer_id.0 == 0 {
                    self.maybe_start_new_access_unit = false;
                    self.count += 1;
                }
            }
        }

        Ok(())
    }
}
