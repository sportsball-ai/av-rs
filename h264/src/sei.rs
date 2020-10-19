use super::{decode, sequence_parameter_set::VUIParameters, syntax_elements::*, Bitstream};

use std::io;

pub const SEI_PAYLOAD_TYPE_PIC_TIMING: u64 = 1;

// ITU-T H.264, 04/2017, 7.3.2.3.1
#[derive(Clone, Debug, Default)]
pub struct SEI {
  pub payload_type: u64,
  pub payload_size: u64,
  pub pic_timing: Option<PicTiming>,
}

#[derive(Clone, Debug, Default)]
pub struct PicTiming {
  pub hours: u8,
  pub minutes: u8,
  pub seconds: u8,
}

impl SEI {
  pub fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>, vui_params: &VUIParameters) -> io::Result<Self> {
    let mut ret = Self::default();

    let mut payload_type = 0;
    let mut payload_size = 0;
    let mut byte = bs.read_bits(8)?;
    while byte == 0xFF {
      payload_type += byte;
      byte = bs.read_bits(8)?;
    }
    payload_type += byte;
    ret.payload_type = payload_type;

    byte = bs.read_bits(8)?;
    while byte == 0xFF {
      payload_size += byte;
      byte = bs.read_bits(8)?;
    }
    payload_size += byte;
    ret.payload_size = payload_size;

    if payload_type == SEI_PAYLOAD_TYPE_PIC_TIMING {
      ret.pic_timing = Some(PicTiming::decode(bs, vui_params)?);
    }

    Ok(ret)
  }
}

impl PicTiming {
  pub fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>, vui_params: &VUIParameters) -> io::Result<Self> {
    let mut ret = Self::default();
    if vui_params.nal_hrd_parameters_present_flag.0 != 0 || vui_params.vcl_hrd_parameters_present_flag.0 != 0 {
      // TODO: read delays
    }

    if vui_params.pic_struct_present_flag.0 == 0 {
      return Ok(ret);
    }

    let pic_struct = bs.read_bits(4)?;
    let num_clock_ts = match pic_struct {
      0..=2 => 1,
      3..=6 => 2,
      _ => 3,
    };

    for _ in 0..num_clock_ts {
      // Checking for clock timestamp flag
      if bs.read_bits(1)? == 0 {
        continue;
      }

      let mut ct_type = U2::default();
      let mut nuit_field_based_flag = U1::default();
      let mut counting_type = U5::default();
      let mut full_timestamp_flag = U1::default();
      let mut discontinuity_flag = U1::default();
      let mut cnt_dropped_flag = U1::default();
      let mut n_frames = U8::default();

      decode!(
        bs,
        &mut ct_type,
        &mut nuit_field_based_flag,
        &mut counting_type,
        &mut full_timestamp_flag,
        &mut discontinuity_flag,
        &mut cnt_dropped_flag,
        &mut n_frames
      )?;

      if full_timestamp_flag.0 == 1 {
        ret.seconds = bs.read_bits(6)? as u8;
        ret.minutes = bs.read_bits(6)? as u8;
        ret.hours = bs.read_bits(5)? as u8;
      } else {
        // Seconds flag
        if bs.read_bits(1)? == 1 {
          ret.seconds = bs.read_bits(6)? as u8;
          // Minutes flag
          if bs.read_bits(1)? == 1 {
            ret.minutes = bs.read_bits(6)? as u8;
            // Hours flag
            if bs.read_bits(1)? == 1 {
              ret.hours = bs.read_bits(5)? as u8;
            }
          }
        }
      }
    }

    Ok(ret)
  }
}
