use crate::bitstream::{Bitstream, BitstreamWriter, Decode};
use crate::{DecodeError, EncodeError};

pub const AF_DESCR_TAG_TIMELINE: u8 = 0x04;

#[derive(Debug, PartialEq, PartialOrd, Copy, Clone)]
pub enum TimeFieldLength {
    None = 0,
    Short = 1,
    Long = 2,
    Reserved = 3,
}

impl Default for TimeFieldLength {
    fn default() -> Self {
        Self::None
    }
}

#[derive(Debug, Default, PartialEq)]
pub struct TEMITimelineDescriptor {
    pub has_timestamp: TimeFieldLength,
    pub timescale: u32,
    pub media_timestamp: u64,

    pub ntp_timestamp: Option<u64>,
    pub ptp_timestamp: Option<u128>,

    pub drop: bool,
    pub frames_per_tc_seconds: u16,
    pub duration: u16,
    pub has_timecode: TimeFieldLength,
    pub time_code: u64,

    pub force_reload: bool,
    pub paused: bool,
    pub discontinuity: bool,
    pub timeline_id: u8,
}

impl TEMITimelineDescriptor {
    pub fn encoded_len(&self) -> usize {
        let mut len = if self.has_timestamp == TimeFieldLength::Short {
            13
        } else if self.has_timestamp == TimeFieldLength::Long {
            17
        } else {
            5
        };
        if self.ntp_timestamp.is_some() {
            len += 8;
        }
        if self.ptp_timestamp.is_some() {
            len += 10;
        }

        len += if self.has_timecode == TimeFieldLength::Short {
            7
        } else if self.has_timecode == TimeFieldLength::Long {
            12
        } else {
            0
        };
        len
    }

    pub fn encode(&self, buf: &mut [u8]) -> Result<usize, EncodeError> {
        let mut bs = BitstreamWriter::new(buf);
        let len = self.encoded_len();

        bs.write_u8(AF_DESCR_TAG_TIMELINE);
        bs.write_u8((len - 2) as u8);
        bs.write_n_bits(self.has_timestamp as u8, 2);
        bs.write_boolean(self.ntp_timestamp.is_some());
        bs.write_boolean(self.ptp_timestamp.is_some());
        bs.write_n_bits(self.has_timecode as u8, 2);
        bs.write_boolean(self.force_reload);
        bs.write_boolean(self.paused);
        bs.write_boolean(self.discontinuity);
        bs.skip_n_bits(7);
        bs.write_u8(self.timeline_id);

        if self.has_timestamp != TimeFieldLength::None {
            bs.write_u32(self.timescale);
            if self.has_timestamp == TimeFieldLength::Short {
                bs.write_u32(self.media_timestamp as u32);
            } else if self.has_timestamp == TimeFieldLength::Long {
                bs.write_u64(self.media_timestamp)
            }
        }

        if let Some(ntp) = self.ntp_timestamp {
            bs.write_u64(ntp);
        }
        if let Some(ptp) = self.ptp_timestamp {
            bs.write_u16((ptp >> 64) as u16);
            bs.write_u64(ptp as u64);
        }

        if self.has_timecode != TimeFieldLength::None {
            bs.write_boolean(self.drop);
            bs.write_n_bits((self.frames_per_tc_seconds >> 8) as u8, 7);
            bs.write_u8(self.frames_per_tc_seconds as u8);
            bs.write_u16(self.duration);
            if self.has_timecode == TimeFieldLength::Short {
                for i in 0..=2 {
                    bs.write_u8((self.time_code >> ((2 - i) * 8)) as u8);
                }
            } else if self.has_timecode == TimeFieldLength::Long {
                bs.write_u64(self.time_code);
            }
        }

        Ok(len)
    }
}

impl Decode for TEMITimelineDescriptor {
    fn decode(bs: &mut Bitstream) -> Result<Self, DecodeError> {
        let has_timestamp = match bs.read_n_bits(2, "has_timestamp")? {
            0 => TimeFieldLength::None,
            1 => TimeFieldLength::Short,
            2 => TimeFieldLength::Long,
            _ => TimeFieldLength::Reserved,
        };

        let has_ntp = bs.read_boolean("has_ntp")?;
        let has_ptp = bs.read_boolean("has_ptp")?;
        let has_timecode = match bs.read_n_bits(2, "has_timecode")? {
            0 => TimeFieldLength::None,
            1 => TimeFieldLength::Short,
            2 => TimeFieldLength::Long,
            _ => TimeFieldLength::Reserved,
        };
        let force_reload = bs.read_boolean("force_reload")?;
        let paused = bs.read_boolean("paused")?;
        let discontinuity = bs.read_boolean("discontinuity")?;
        bs.skip_bits(7);
        let timeline_id = bs.read_u8("timeline_id")?;
        let mut ret = TEMITimelineDescriptor {
            has_timestamp,
            has_timecode,
            force_reload,
            paused,
            discontinuity,
            timeline_id,
            ..Default::default()
        };

        if ret.has_timestamp != TimeFieldLength::None {
            ret.timescale = bs.read_u32("timescale")?;
            if ret.has_timestamp == TimeFieldLength::Short {
                ret.media_timestamp = bs.read_u32("media_timestamp32")? as u64;
            } else if ret.has_timestamp == TimeFieldLength::Long {
                ret.media_timestamp = bs.read_u64("media_timestamp64")?;
            }
        }
        if has_ntp {
            ret.ntp_timestamp = bs.read_u64("ntp_timestamp").ok();
        }
        if has_ptp {
            ret.ptp_timestamp = Some((bs.read_u16("ptp_timestamp high")? as u128) << 64 | bs.read_u64("ptp_timestamp low")? as u128);
        }
        if ret.has_timecode != TimeFieldLength::None {
            ret.drop = bs.read_boolean("drop")?;
            ret.frames_per_tc_seconds = (bs.read_n_bits(7, "frames_per_tc_seconds high")? as u16) << 8 | bs.read_u8("frames_per_tc_seconds low")? as u16;
            ret.duration = bs.read_u16("duration")?;
            if ret.has_timecode == TimeFieldLength::Short {
                ret.time_code = (bs.read_u8("short_time_code high")? as u64) << 16 | bs.read_u16("short_time_code low")? as u64;
            } else if ret.has_timecode == TimeFieldLength::Long {
                ret.time_code = bs.read_u64("long_time_code")?;
            }
        }
        Ok(ret)
    }
}

#[cfg(test)]
mod test {
    use crate::bitstream::{Bitstream, BitstreamWriter, Decode};
    use crate::temi::{TEMITimelineDescriptor, TimeFieldLength};

    #[test]
    fn test_default_encode_decode() {
        let temi = TEMITimelineDescriptor::default();

        let mut w: Vec<u8> = vec![0x0u8; temi.encoded_len()];
        let mut bs = BitstreamWriter::new(&mut w[..]);

        let len = temi.encode(bs.inner_remaining()).unwrap();
        assert_eq!(len, 5);
        assert_eq!(w, [4, 3, 0, 0, 0]);

        let mut bitstream = Bitstream::new(&w[2..]);
        let _ = TEMITimelineDescriptor::decode(&mut bitstream).unwrap();
    }

    #[test]
    fn test_encode_decode() {
        let temi = TEMITimelineDescriptor {
            has_timestamp: TimeFieldLength::Long,
            timescale: 0xEBF1_3405,
            media_timestamp: 0xa1dd_3e6a_19d7_bfec,

            ntp_timestamp: Some(0xfdc9_b5f8_25e9_9236),
            ptp_timestamp: Some(0xcdf8_fdc9_b5f8_25e9_9236),

            has_timecode: TimeFieldLength::Short,
            drop: true,
            frames_per_tc_seconds: 0x75a3,
            duration: 0x988a,
            time_code: 0xab_82ef,

            force_reload: true,
            paused: true,
            discontinuity: true,
            timeline_id: 0xf3,
        };

        let mut w: Vec<u8> = vec![0x0u8; temi.encoded_len()];
        let mut bs = BitstreamWriter::new(&mut w[..]);
        let len = temi.encode(bs.inner_remaining()).unwrap();
        assert_eq!(len, temi.encoded_len());
        assert_eq!(len, 2 + 3 + 12 + 8 + 10 + 4 + 3);

        let mut bitstream = Bitstream::new(&w[2..]);
        let decoded = TEMITimelineDescriptor::decode(&mut bitstream).unwrap();
        assert_eq!(decoded, temi);
    }

    #[test]
    fn test_ntp_without_media_timestamp() {
        let temi = TEMITimelineDescriptor {
            has_timestamp: TimeFieldLength::None,
            timescale: 0,
            media_timestamp: 0,

            ntp_timestamp: Some(0xfdc9_b5f8_25e9_9236),
            ptp_timestamp: None,

            has_timecode: TimeFieldLength::Short,
            drop: true,
            frames_per_tc_seconds: 0x75a3,
            duration: 0x988a,
            time_code: 0xab_82ef,

            force_reload: true,
            paused: true,
            discontinuity: false,
            timeline_id: 0xf3,
        };

        let mut w: Vec<u8> = vec![0x0u8; temi.encoded_len()];
        let mut bs = BitstreamWriter::new(&mut w[..]);
        let len = temi.encode(bs.inner_remaining()).unwrap();
        assert_eq!(len, temi.encoded_len());
        assert_eq!(len, 2 + 3 + 8 + 4 + 3);

        let mut bitstream = Bitstream::new(&w[2..]);
        let decoded = TEMITimelineDescriptor::decode(&mut bitstream).unwrap();
        assert_eq!(decoded, temi);
    }
}
