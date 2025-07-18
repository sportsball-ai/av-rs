use super::{decode, encode, sequence_parameter_set::VUIParameters, syntax_elements::*, Bitstream, BitstreamWriter, Decode, Encode};

use std::io;

pub const SEI_PAYLOAD_TYPE_PIC_TIMING: u64 = 1;
pub const SEI_PAYLOAD_TYPE_USER_DATA_UNREGISTERED: u64 = 5;

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

impl Encode for SEI {
    fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>) -> io::Result<()> {
        for msg in &self.sei_message {
            msg.encode(bs)?;
        }
        bs.write_bits(0x80, 8)
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

impl Encode for SEIMessage {
    fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>) -> io::Result<()> {
        let mut payload_type = self.payload_type;
        while payload_type >= 0xFF {
            bs.write_bits(0xFF, 8)?;
            payload_type -= 0xFF;
        }
        bs.write_bits(payload_type, 8)?;

        let mut payload_size = self.payload.len();
        while payload_size >= 0xFF {
            bs.write_bits(0xFF, 8)?;
            payload_size -= 0xFF;
        }
        bs.write_bits(payload_size as u64, 8)?;

        bs.write_bytes(&self.payload)
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

        let hrd_params = vui_params.nal_hrd_parameters.as_ref().or(vui_params.vcl_hrd_parameters.as_ref());
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

    pub fn encode<W: io::Write>(&self, bs: &mut BitstreamWriter<W>, vui_params: &VUIParameters) -> io::Result<()> {
        let hrd_params = vui_params.nal_hrd_parameters.as_ref().or(vui_params.vcl_hrd_parameters.as_ref());
        if let Some(hrd_params) = hrd_params {
            bs.write_bits(self.cpb_removal_delay, hrd_params.cpb_removal_delay_length_minus1.0 as usize + 1)?;
            bs.write_bits(self.dpb_output_delay, hrd_params.dpb_output_delay_length_minus1.0 as usize + 1)?;
        }

        if vui_params.pic_struct_present_flag.0 == 0 {
            return Ok(());
        }

        encode!(bs, &self.pic_struct)?;

        for timecode in &self.timecodes {
            encode!(bs, &timecode.clock_timestamp_flag)?;
            if timecode.clock_timestamp_flag.0 == 0 {
                continue;
            }

            encode!(
                bs,
                &timecode.ct_type,
                &timecode.nuit_field_based_flag,
                &timecode.counting_type,
                &timecode.full_timestamp_flag,
                &timecode.discontinuity_flag,
                &timecode.cnt_dropped_flag,
                &timecode.n_frames
            )?;

            if timecode.full_timestamp_flag.0 == 1 {
                encode!(bs, &timecode.seconds, &timecode.minutes, &timecode.hours)?;
            } else {
                encode!(bs, &timecode.seconds_flag)?;
                if timecode.seconds_flag.0 == 1 {
                    encode!(bs, &timecode.seconds, &timecode.minutes_flag)?;
                    if timecode.minutes_flag.0 == 1 {
                        encode!(bs, &timecode.minutes, &timecode.hours_flag)?;
                        if timecode.hours_flag.0 == 1 {
                            encode!(bs, &timecode.hours)?;
                        }
                    }
                }
            }

            let time_offset_length = hrd_params.map(|p| p.time_offset_length.0 as usize).unwrap_or(24);
            bs.write_bits(timecode.time_offset.0 as _, time_offset_length)?;
        }

        Ok(())
    }

    pub fn num_clock_ts(&self) -> usize {
        match self.pic_struct.0 {
            3..=4 | 7 => 2,
            5..=6 | 8 => 3,
            _ => 1,
        }
    }
}

pub const UUID_ISO_IEC_11578_AVCHD_METADATA: u128 = 0x17ee8c60f84d11d98cd60800200c9a66;

#[derive(Clone, Debug, Default)]
pub struct UserDataUnregistered {
    pub uuid_iso_iec_11578: u128,
    pub user_data_payload: Vec<u8>,
}

impl UserDataUnregistered {
    pub fn decode<T: Iterator<Item = u8>>(mut bs: Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();
        ret.uuid_iso_iec_11578 = (bs.read_bits(64)? as u128) << 64 | bs.read_bits(64)? as u128;
        ret.user_data_payload = bs.into_inner().collect();
        Ok(ret)
    }
}

#[derive(Clone, Debug, Default)]
pub struct AVCHDMetadata {
    pub tags: Vec<(u8, u32)>,
}

#[derive(Clone, Debug, Default)]
pub struct AVCHDMetadataDateTime {
    pub year: u16,
    pub month: u8,
    pub day: u8,
    pub hour: u8,
    pub minute: u8,
    pub second: u8,
}

fn bcd_decode(bcd: u8) -> u8 {
    (bcd >> 4) * 10 + (bcd & 0x0F)
}

impl AVCHDMetadata {
    pub fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        let magic = bs.read_bits(32)?;
        if magic != 0x4d44504d {
            return Err(io::Error::new(io::ErrorKind::Other, "unexpected avchd metadata magic"));
        }

        let n = bs.read_bits(8)? as usize;
        for _ in 0..n {
            let tag = bs.read_bits(8)? as u8;
            let value = bs.read_bits(32)? as u32;
            ret.tags.push((tag, value));
        }

        Ok(ret)
    }

    pub fn datetime(&self) -> Option<AVCHDMetadataDateTime> {
        let mut ret = AVCHDMetadataDateTime::default();

        for (key, value) in &self.tags {
            match key {
                0x18 => {
                    ret.year = bcd_decode((value >> 16) as _) as u16 * 100 + bcd_decode((value >> 8) as _) as u16;
                    ret.month = bcd_decode(*value as _);
                }
                0x19 => {
                    ret.day = bcd_decode((value >> 24) as _);
                    ret.hour = bcd_decode((value >> 16) as _);
                    ret.minute = bcd_decode((value >> 8) as _);
                    ret.second = bcd_decode(*value as _);
                }
                _ => {}
            }
        }

        if ret.year != 0 && ret.day != 0 {
            Some(ret)
        } else {
            None
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

        let data = vec![
            0x00, 0x07, 0x80, 0xae, 0x19, 0x00, 0x01, 0xaf, 0x40, 0x01, 0x0c, 0x00, 0x00, 0x44, 0x00, 0x00, 0x02, 0x08, 0x24, 0x1c, 0x29, 0x00, 0x40, 0x04,
            0x47, 0xb5, 0x00, 0x31, 0x47, 0x41, 0x39, 0x34, 0x03, 0xd4, 0xff, 0xfc, 0x80, 0x80, 0xfd, 0x80, 0x80, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa,
            0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa,
            0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xff,
            0x80,
        ];
        let mut bs = Bitstream::new(data.iter().copied());
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

        let mut encoded = vec![];
        sei.encode(&mut BitstreamWriter::new(&mut encoded)).unwrap();
        assert_eq!(encoded, data);
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

    #[test]
    fn test_avchd_metadata() {
        let bs = Bitstream::new(vec![
            0x17, 0xee, 0x8c, 0x60, 0xf8, 0x4d, 0x11, 0xd9, 0x8c, 0xd6, 0x8, 0x0, 0x20, 0xc, 0x9a, 0x66, 0x4d, 0x44, 0x50, 0x4d, 0x7, 0x13, 0x64, 0x32, 0x51,
            0x1, 0x14, 0x0, 0x0, 0x0, 0x0, 0x18, 0x2a, 0x20, 0x24, 0x9, 0x19, 0x15, 0x12, 0x26, 0x22, 0xe0, 0x1, 0x3, 0x4, 0x9, 0xe6, 0x0, 0x1, 0x0, 0x0, 0xe8,
            0xf, 0x0, 0x0, 0x0,
        ]);

        let msg = UserDataUnregistered::decode(bs).unwrap();
        assert_eq!(UUID_ISO_IEC_11578_AVCHD_METADATA, msg.uuid_iso_iec_11578);

        let metadata = AVCHDMetadata::decode(&mut Bitstream::new(msg.user_data_payload)).unwrap();
        let t = metadata.datetime().unwrap();
        assert_eq!(t.year, 2024);
        assert_eq!(t.month, 9);
        assert_eq!(t.day, 15);
        assert_eq!(t.hour, 12);
        assert_eq!(t.minute, 26);
        assert_eq!(t.second, 22);
    }
}
