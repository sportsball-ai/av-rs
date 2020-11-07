use super::{decode, sequence_parameter_set::VUIParameters, syntax_elements::*, Bitstream, Decode};

use std::io;

pub const SEI_PAYLOAD_TYPE_PIC_TIMING: u64 = 1;

// ITU-T H.264, 04/2017, 7.3.2.3.1
#[derive(Clone, Debug, Default)]
pub struct SEI {
    pub sei_message: Vec<SEIMessage>,
}

impl Decode for SEI {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();
        loop {
            ret.sei_message.push(SEIMessage::decode(bs)?);
            if !bs.more_non_slice_rbsp_data() {
                break;
            }
        }
        Ok(ret)
    }
}

// ITU-T H.264, 04/2017, 7.3.2.3.1
#[derive(Clone, Debug, Default)]
pub struct SEIMessage {
    pub payload_type: u64,
    pub payload: Vec<u8>,
}

impl Decode for SEIMessage {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
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

        ret.payload = bs.read_bytes(payload_size as _)?;

        Ok(ret)
    }
}

#[derive(Clone, Debug, Default)]
pub struct PicTiming {
    pub cpb_removal_delay: u64,
    pub dpb_output_delay: u64,
    pub pic_struct: U4,
    pub timecodes: Vec<Timecode>,
}

#[derive(Clone, Debug, Default)]
pub struct Timecode {
    pub clock_timestamp_flag: U1,
    // if (clock_timestamp_flag) {
    pub ct_type: U2,
    pub nuit_field_based_flag: U1,
    pub counting_type: U5,
    pub full_timestamp_flag: U1,
    pub discontinuity_flag: U1,
    pub cnt_dropped_flag: U1,
    pub n_frames: U8,
    pub seconds: U6,
    pub minutes: U6,
    pub hours: U5,
    pub seconds_flag: U1,
    pub minutes_flag: U1,
    pub hours_flag: U1,
    // }
    pub time_offset: U32,
}

impl PicTiming {
    pub fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>, vui_params: &VUIParameters) -> io::Result<Self> {
        let mut ret = Self::default();

        let hrd_params = vui_params.nal_hrd_parameters.as_ref().or_else(||vui_params.vcl_hrd_parameters.as_ref());
        if let Some(hrd_params) = hrd_params {
            ret.cpb_removal_delay = bs.read_bits(hrd_params.cpb_removal_delay_length_minus1.0 as usize + 1)?;
            ret.dpb_output_delay = bs.read_bits(hrd_params.dpb_output_delay_length_minus1.0 as usize + 1)?;
        }

        if vui_params.pic_struct_present_flag.0 == 0 {
            return Ok(ret);
        }

        decode!(bs, &mut ret.pic_struct)?;

        for _ in 0..ret.num_clock_ts() {
            let mut timecode = Timecode::default();

            decode!(bs, &mut timecode.clock_timestamp_flag)?;
            if timecode.clock_timestamp_flag.0 == 0 {
                continue;
            }

            decode!(
                bs,
                &mut timecode.ct_type,
                &mut timecode.nuit_field_based_flag,
                &mut timecode.counting_type,
                &mut timecode.full_timestamp_flag,
                &mut timecode.discontinuity_flag,
                &mut timecode.cnt_dropped_flag,
                &mut timecode.n_frames
            )?;

            if timecode.full_timestamp_flag.0 == 1 {
                decode!(bs, &mut timecode.seconds, &mut timecode.minutes, &mut timecode.hours)?;
            } else {
                decode!(bs, &mut timecode.seconds_flag)?;
                if timecode.seconds_flag.0 == 1 {
                    decode!(bs, &mut timecode.seconds, &mut timecode.minutes_flag)?;
                    if timecode.minutes_flag.0 == 1 {
                        decode!(bs, &mut timecode.minutes, &mut timecode.hours_flag)?;
                        if timecode.hours_flag.0 == 1 {
                            decode!(bs, &mut timecode.hours)?;
                        }
                    }
                }
            }

            let time_offset_length = hrd_params.map(|p| p.time_offset_length.0 as usize).unwrap_or(24);
            timecode.time_offset.0 = bs.read_bits(time_offset_length)? as u32;

            ret.timecodes.push(timecode);
        }

        Ok(ret)
    }

    pub fn num_clock_ts(&self) -> usize {
        match self.pic_struct.0 {
            3..=4 | 7 => 2,
            5..=6 | 8 => 3,
            _ => 1,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::bitstream::Decode;
    use crate::sequence_parameter_set::*;

    #[test]
    fn test_sei() {
        let mut bs = Bitstream::new(vec![
            0x64, 0x00, 0x1f, 0xac, 0x72, 0x30, 0x14, 0x01, 0x6e, 0xc0, 0x44, 0x00, 0x00, 0x0f, 0xa4, 0x00, 0x03, 0xa9, 0x83, 0x89, 0x80, 0x03, 0xd0, 0x90,
            0x00, 0x7a, 0x13, 0xbd, 0xee, 0x03, 0xe1, 0x10, 0x8a, 0x70,
        ]);
        let sps = SequenceParameterSet::decode(&mut bs).unwrap();

        let mut bs = Bitstream::new(vec![
            0x00, 0x07, 0x80, 0xae, 0x19, 0x00, 0x01, 0xaf, 0x40, 0x01, 0x0c, 0x00, 0x00, 0x44, 0x00, 0x00, 0x02, 0x08, 0x24, 0x1c, 0x29, 0x00, 0x40, 0x04,
            0x47, 0xb5, 0x00, 0x31, 0x47, 0x41, 0x39, 0x34, 0x03, 0xd4, 0xff, 0xfc, 0x80, 0x80, 0xfd, 0x80, 0x80, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa,
            0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa,
            0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xff,
            0x80,
        ]);
        let sei = SEI::decode(&mut bs).unwrap();
        assert_eq!(3, sei.sei_message.len());
        assert_eq!(0, sei.sei_message[0].payload_type);
        assert_eq!(1, sei.sei_message[1].payload_type);
        assert_eq!(4, sei.sei_message[2].payload_type);

        let pic_timing = PicTiming::decode(&mut Bitstream::new(sei.sei_message[1].payload.iter().copied()), &sps.vui_parameters).unwrap();
        assert_eq!(68, pic_timing.cpb_removal_delay);
        assert_eq!(2, pic_timing.dpb_output_delay);
        assert_eq!(0, pic_timing.pic_struct.0);
        assert_eq!(1, pic_timing.timecodes.len());

        let timecode = &pic_timing.timecodes[0];
        assert_eq!(28, timecode.n_frames.0);
        assert_eq!(10, timecode.seconds.0);
        assert_eq!(16, timecode.minutes.0);
        assert_eq!(0, timecode.hours.0);
        assert_eq!(1, timecode.clock_timestamp_flag.0);
        assert_eq!(0, timecode.ct_type.0);
        assert_eq!(0, timecode.nuit_field_based_flag.0);
        assert_eq!(4, timecode.counting_type.0);
        assert_eq!(1, timecode.full_timestamp_flag.0);
        assert_eq!(0, timecode.discontinuity_flag.0);
        assert_eq!(0, timecode.cnt_dropped_flag.0);
        assert_eq!(0, timecode.seconds_flag.0);
        assert_eq!(0, timecode.minutes_flag.0);
        assert_eq!(0, timecode.hours_flag.0);
        assert_eq!(0, timecode.time_offset.0);
    }

    #[test]
    fn test_pic_timing() {
        let mut bs = Bitstream::new(vec![
            0x4d, 0x40, 0x28, 0x8d, 0x95, 0x80, 0xf0, 0x08, 0x8f, 0xbc, 0x04, 0x40, 0x00, 0x00, 0xfa, 0x40, 0x00, 0x3a, 0x98, 0x25,
        ]);
        let sps = SequenceParameterSet::decode(&mut bs).unwrap();

        let mut bs = Bitstream::new(vec![0x01, 0x09, 0x1a, 0x24, 0x02, 0x6b, 0x99, 0x00, 0x00, 0x00, 0x40, 0x80]);
        let sei = SEI::decode(&mut bs).unwrap();
        assert_eq!(1, sei.sei_message.len());

        let msg = sei.sei_message.into_iter().next().unwrap();
        assert_eq!(1, sps.vui_parameters.pic_struct_present_flag.0);
        assert_eq!(1, msg.payload_type);
        assert_eq!(9, msg.payload.len());

        let pic_timing = PicTiming::decode(&mut Bitstream::new(msg.payload), &sps.vui_parameters).unwrap();
        assert_eq!(0, pic_timing.cpb_removal_delay);
        assert_eq!(0, pic_timing.dpb_output_delay);
        assert_eq!(1, pic_timing.pic_struct.0);
        assert_eq!(1, pic_timing.timecodes.len());

        let timecode = &pic_timing.timecodes[0];
        assert_eq!(2, timecode.n_frames.0);
        assert_eq!(26, timecode.seconds.0);
        assert_eq!(57, timecode.minutes.0);
        assert_eq!(18, timecode.hours.0);
        assert_eq!(1, timecode.clock_timestamp_flag.0);
        assert_eq!(1, timecode.ct_type.0);
        assert_eq!(0, timecode.nuit_field_based_flag.0);
        assert_eq!(4, timecode.counting_type.0);
        assert_eq!(1, timecode.full_timestamp_flag.0);
        assert_eq!(0, timecode.discontinuity_flag.0);
        assert_eq!(0, timecode.cnt_dropped_flag.0);
        assert_eq!(0, timecode.seconds_flag.0);
        assert_eq!(0, timecode.minutes_flag.0);
        assert_eq!(0, timecode.hours_flag.0);
        assert_eq!(0, timecode.time_offset.0);
    }
}
