use crate::{DecodeError, EncodeError};
use core2::io::Write;

const AF_DESCR_TAG_TIMELINE: u8 = 0x04;

#[derive(Debug, Default, PartialEq)]
pub struct TEMITimelineDescriptor {
    pub has_timestamp: Option<u8>,
    pub timescale: u32,
    pub media_timestamp: u64,

    pub ntp_timestamp: Option<u64>,
    pub ptp_timestamp: Option<u128>,

    pub has_timecode: Option<u8>,
    pub drop: bool,
    pub frames_per_tc_seconds: u16,
    pub duration: u16,
    pub time_code: u64,

    pub force_reload: Option<bool>,
    pub paused: Option<bool>,
    pub discontinuity: Option<bool>,
    pub timeline_id: u8,
}

impl TEMITimelineDescriptor {
    pub fn encode_len(&self) -> usize {
        let mut len = 5;
        if let Some(ts) = self.has_timestamp {
            // timescale + media_timestamp
            if ts == 1 {
                len += 8;
            } else if ts == 2 {
                len += 12;
            }
        }
        if self.ntp_timestamp.is_some() {
            len += 8;
        } else if self.ptp_timestamp.is_some() {
            len += 10;
        }
        if let Some(timecode) = self.has_timecode {
            len += 4;
            if timecode == 1 {
                len += 3;
            } else {
                len += 8;
            }
        }
        len
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        let mut buf = vec![0u8; self.encode_len()];
        buf[0] = AF_DESCR_TAG_TIMELINE;
        if let Some(timestamp) = self.has_timestamp {
            buf[2] |= timestamp << 6;
        }

        if self.force_reload.unwrap_or(false) {
            buf[2] |= 0b0000_0010;
        }
        if self.paused.unwrap_or(false) {
            buf[2] |= 0b0000_0001;
        }
        if self.discontinuity.unwrap_or(false) {
            buf[3] = 0b1000_0000;
        }
        buf[4] = self.timeline_id;

        let mut ret = 5;
        if let Some(has_timestamp) = self.has_timestamp {
            buf[5] = self.timescale as _;
            buf[6] = (self.timescale >> 8) as u8;
            buf[7] = (self.timescale >> 16) as u8;
            buf[8] = (self.timescale >> 24) as u8;

            buf[9] = (self.media_timestamp) as _;
            buf[10] = (self.media_timestamp >> 8) as u8;
            buf[11] = (self.media_timestamp >> 16) as u8;
            buf[12] = (self.media_timestamp >> 24) as u8;
            ret += 8;
            if has_timestamp == 2 {
                buf[13] = (self.media_timestamp >> 32) as u8;
                buf[14] = (self.media_timestamp >> 40) as u8;
                buf[15] = (self.media_timestamp >> 48) as u8;
                buf[16] = (self.media_timestamp >> 56) as u8;
                ret += 4;
            } else if has_timestamp != 1 {
                return Err(EncodeError::other("Invalid has_timestamp value"));
            }
        }

        if let Some(ntp_ts) = self.ntp_timestamp {
            buf[2] |= 0b0010_0000;
            for i in 0..8 {
                buf[ret + i] = (ntp_ts >> (i * 8)) as u8;
            }
            ret += 8;
        } else if let Some(ptp_ts) = self.ptp_timestamp {
            buf[2] |= 0b0001_0000;
            for i in 0..10 {
                buf[ret + i] = (ptp_ts >> (i * 8)) as u8;
            }
            ret += 10;
        }

        if let Some(has_timecode) = self.has_timecode {
            if has_timecode != 0 {
                buf[2] |= has_timecode << 2;
                if self.drop {
                    buf[ret] = 0b1000_0000;
                }
                buf[ret] |= (self.frames_per_tc_seconds >> 8) as u8 & 0b0111_1111;
                buf[ret + 1] = self.frames_per_tc_seconds as _;
                buf[ret + 2] = (self.duration >> 8) as u8;
                buf[ret + 3] = self.duration as _;

                ret += 4;
                for i in 0..3 {
                    buf[ret + i] = (self.time_code >> (i * 8)) as u8;
                }
                ret += 3;
                if has_timecode == 2 {
                    for i in 0..5 {
                        buf[ret + i] = (self.time_code >> ((i + 3) * 8)) as u8;
                    }
                    ret += 8;
                } else if has_timecode != 1 {
                    return Err(EncodeError::other("Invalid has_timecode value"));
                }
            }
        }

        buf[1] = ret as u8 - 2;

        w.write_all(&buf)?;
        Ok(buf.len())
    }

    pub fn decode(buf: &[u8]) -> Result<Self, DecodeError> {
        if buf.len() < 5 || (buf[1] as usize + 2) > buf.len() {
            println!("buf[1]={}", buf[1]);
            return Err(DecodeError::new("not enough bytes for temi_timeline_descriptor"));
        }
        if buf[0] != AF_DESCR_TAG_TIMELINE {
            return Err(DecodeError::new("incorrect tag value for temi_timeline_descriptor"));
        }
        let mut ret = TEMITimelineDescriptor {
            force_reload: Some(buf[2] & 0b0000_0010 != 0),
            paused: Some(buf[2] & 0b0000_0001 != 0),
            discontinuity: Some(buf[3] & 0b1000_0000 != 0),
            timeline_id: buf[4],
            ..Self::default()
        };

        let has_timestamp = buf[2] >> 6;

        let mut n = 5;
        if has_timestamp != 0 {
            ret.timescale = buf[5] as u32 | (buf[6] as u32) << 8 | (buf[7] as u32) << 16 | (buf[8] as u32) << 24;
            if has_timestamp <= 2 {
                ret.media_timestamp = buf[9] as u64 | (buf[10] as u64) << 8 | (buf[11] as u64) << 16 | (buf[12] as u64) << 24;
                n += 8;
            } else {
                return Err(DecodeError::new("incorrect media_timestamp for temi_timeline_descriptor"));
            }
            if has_timestamp == 2 {
                n += 4;
                ret.media_timestamp |= (buf[13] as u64 | (buf[14] as u64) << 8 | (buf[15] as u64) << 16 | (buf[16] as u64) << 24) << 32;
            }
            ret.has_timestamp = Some(has_timestamp);
        }

        if buf[2] & 0b0010_0000 != 0 {
            let mut ts = 0u64;
            for i in 0..8 {
                ts |= (buf[n + i] as u64) << (i * 8);
            }
            n += 8;
            ret.ntp_timestamp = Some(ts);
        } else if buf[2] & 0b0001_0000 != 0 {
            let mut ts = 0u128;
            for i in 0..10 {
                ts |= (buf[n + i] as u128) << (i * 8);
            }
            n += 10;
            ret.ptp_timestamp = Some(ts);
        }

        let has_timecode = (buf[2] & 0b0000_1100) >> 2;
        if has_timecode != 0 {
            ret.drop = buf[n] >> 7 == 1;
            ret.frames_per_tc_seconds = (buf[n] as u16 & 0b0111_1111) << 8 | buf[n + 1] as u16;
            ret.duration = (buf[n + 2] as u16) << 8 | buf[n + 3] as u16;
            n += 4;

            if has_timecode <= 2 {
                ret.time_code = buf[n] as u64 | (buf[n + 1] as u64) << 8 | (buf[n + 2] as u64) << 16;
                if has_timecode == 2 {
                    n += 3;
                    ret.time_code |=
                        (buf[n] as u64 | (buf[n + 1] as u64) << 8 | (buf[n + 2] as u64) << 16 | (buf[n + 3] as u64) << 24 | (buf[n + 4] as u64) << 32) << 24;
                }
            } else {
                return Err(DecodeError::new("incorrect has_timecode for temi_timeline_descriptor"));
            }

            ret.has_timecode = Some(has_timecode);
        }
        Ok(ret)
    }
}

#[cfg(test)]
mod test {
    use crate::temi::TEMITimelineDescriptor;

    #[test]
    fn test_default_encode_decode() {
        let mut w: Vec<u8> = vec![];
        let temi = TEMITimelineDescriptor::default();
        let len = temi.encode(&mut w).unwrap();
        assert_eq!(len, 5);
        assert_eq!(w, [4, 3, 0, 0, 0]);

        let _ = TEMITimelineDescriptor::decode(&w[..]).unwrap();
    }

    #[test]
    fn test_ntp_encode_decode() {
        let mut w: Vec<u8> = vec![];
        let temi = TEMITimelineDescriptor {
            has_timestamp: Some(2),
            timescale: 0xEBF1_3405,
            media_timestamp: 0xa1dd_3e6a_19d7_bfec,

            ntp_timestamp: Some(0xfdc9_b5f8_25e9_9236),
            ptp_timestamp: None,

            has_timecode: Some(1),
            drop: true,
            frames_per_tc_seconds: 0x75a3,
            duration: 0x988a,
            time_code: 0xab_82ef,

            force_reload: Some(true),
            paused: Some(true),
            discontinuity: Some(true),
            timeline_id: 0xf3,
        };

        let len = temi.encode(&mut w).unwrap();
        assert_eq!(len, temi.encode_len());
        assert_eq!(len, 2 + 3 + 12 + 8 + 4 + 3);

        let decoded = TEMITimelineDescriptor::decode(&w).unwrap();
        assert_eq!(decoded, temi);
    }

    #[test]
    fn test_ptp_encode_decode() {
        let mut w: Vec<u8> = vec![];
        let temi = TEMITimelineDescriptor {
            has_timestamp: Some(1),
            timescale: 0xEBF1_3405,
            media_timestamp: 0x19d7_bfec,

            ntp_timestamp: None,
            ptp_timestamp: Some(0xcdf8_fdc9_b5f8_25e9_9236),

            has_timecode: Some(2),
            drop: true,
            frames_per_tc_seconds: 0x75a3,
            duration: 0x988a,
            time_code: 0x9328_832a_e0ab_82ef,

            force_reload: Some(true),
            paused: Some(true),
            discontinuity: Some(true),
            timeline_id: 0xf3,
        };

        let len = temi.encode(&mut w).unwrap();
        assert_eq!(len, temi.encode_len());

        let decoded = TEMITimelineDescriptor::decode(&w).unwrap();
        assert_eq!(decoded, temi);
    }
}
