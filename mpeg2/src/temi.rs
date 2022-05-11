use crate::{DecodeError, EncodeError};
use byteorder::{BigEndian, ByteOrder};
use core2::io::Write;

pub const AF_DESCR_TAG_TIMELINE: u8 = 0x04;

#[derive(Debug, PartialEq, PartialOrd, Copy, Clone)]
pub enum TimeFieldLength {
    None = 0,
    Short = 1,
    Long = 2,
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

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        let mut buf = vec![0u8; self.encoded_len()];
        buf[0] = AF_DESCR_TAG_TIMELINE;
        buf[2] = (self.has_timestamp as u8) << 6;

        if self.force_reload {
            buf[2] |= 0b0000_0010;
        }
        if self.paused {
            buf[2] |= 0b0000_0001;
        }
        if self.discontinuity {
            buf[3] = 0b1000_0000;
        }
        buf[4] = self.timeline_id;

        let mut ret = 5;
        if self.has_timestamp != TimeFieldLength::None {
            BigEndian::write_u32(&mut buf[5..9], self.timescale);
            if self.has_timestamp == TimeFieldLength::Short {
                BigEndian::write_u32(&mut buf[9..13], self.media_timestamp as u32);
                ret += 8;
            } else if self.has_timestamp == TimeFieldLength::Long {
                BigEndian::write_u64(&mut buf[9..17], self.media_timestamp);
                ret += 12;
            }
        }

        if let Some(ntp_ts) = self.ntp_timestamp {
            buf[2] |= 0b0010_0000;
            BigEndian::write_u64(&mut buf[ret..], ntp_ts);
            ret += 8;
        }
        if let Some(ptp_ts) = self.ptp_timestamp {
            buf[2] |= 0b0001_0000;
            BigEndian::write_u16(&mut buf[ret..], (ptp_ts >> 64) as u16);
            ret += 2;
            BigEndian::write_u64(&mut buf[ret..], ptp_ts as u64);
            ret += 8;
        }

        if self.has_timecode != TimeFieldLength::None {
            buf[2] |= (self.has_timecode as u8) << 2;

            BigEndian::write_u16(&mut buf[ret..], self.frames_per_tc_seconds & 0x7fff);
            BigEndian::write_u16(&mut buf[ret + 2..], self.duration);
            if self.drop {
                buf[ret] |= 0b1000_0000;
            }

            ret += 4;
            if self.has_timecode == TimeFieldLength::Short {
                for i in 0..=2 {
                    buf[ret + i] = (self.time_code >> ((2 - i) * 8)) as u8;
                }
                ret += 3;
            } else {
                BigEndian::write_u64(&mut buf[ret..], self.time_code);
                ret += 8;
            }
        }

        buf[1] = ret as u8 - 2;

        w.write_all(&buf)?;
        Ok(ret)
    }

    pub fn decode(buf: &[u8]) -> Result<Self, DecodeError> {
        if buf.len() < 5 || (buf[1] as usize + 2) > buf.len() {
            return Err(DecodeError::new("not enough bytes for temi_timeline_descriptor"));
        }
        if buf[0] != AF_DESCR_TAG_TIMELINE {
            return Err(DecodeError::new("incorrect tag value for temi_timeline_descriptor"));
        }
        let mut ret = TEMITimelineDescriptor {
            force_reload: buf[2] & 0b0000_0010 != 0,
            paused: buf[2] & 0b0000_0001 != 0,
            discontinuity: buf[3] & 0b1000_0000 != 0,
            timeline_id: buf[4],
            ..Self::default()
        };

        match buf[2] >> 6 {
            0 => ret.has_timestamp = TimeFieldLength::None,
            1 => ret.has_timestamp = TimeFieldLength::Short,
            2 => ret.has_timestamp = TimeFieldLength::Long,
            _ => return Err(DecodeError::new("incorrect has_timestamp")),
        }

        let mut n = 5;
        if ret.has_timestamp != TimeFieldLength::None {
            ret.timescale = BigEndian::read_u32(&buf[5..9]);
            if ret.has_timestamp == TimeFieldLength::Short {
                ret.media_timestamp = BigEndian::read_u32(&buf[9..=12]) as u64;
                n += 8;
            } else {
                ret.media_timestamp = BigEndian::read_u64(&buf[9..17]);
                n += 12;
            }
        }

        if buf[2] & 0b0010_0000 != 0 {
            let ts = BigEndian::read_u64(&buf[n..]);
            n += 8;
            ret.ntp_timestamp = Some(ts);
        }
        if buf[2] & 0b0001_0000 != 0 {
            let mut ts = (BigEndian::read_u16(&buf[n..]) as u128) << 64;
            n += 2;
            ts |= BigEndian::read_u64(&buf[n..]) as u128;
            n += 8;
            ret.ptp_timestamp = Some(ts);
        }

        match (buf[2] & 0b0000_1100) >> 2 {
            0 => ret.has_timecode = TimeFieldLength::None,
            1 => ret.has_timecode = TimeFieldLength::Short,
            2 => ret.has_timecode = TimeFieldLength::Long,
            _ => return Err(DecodeError::new("incorrect has_timecode")),
        }
        if ret.has_timecode != TimeFieldLength::None {
            ret.drop = buf[n] >> 7 == 1;
            ret.frames_per_tc_seconds = (buf[n] as u16 & 0b0111_1111) << 8 | buf[n + 1] as u16;
            ret.duration = (buf[n + 2] as u16) << 8 | buf[n + 3] as u16;
            n += 4;
            if ret.has_timecode == TimeFieldLength::Short {
                ret.time_code = (buf[n] as u64) << 16 | (buf[n + 1] as u64) << 8 | buf[n + 2] as u64;
            } else {
                ret.time_code = BigEndian::read_u64(&buf[n..]);
            }
        }
        Ok(ret)
    }
}

#[cfg(test)]
mod test {
    use crate::temi::{TEMITimelineDescriptor, TimeFieldLength};

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
    fn test_encode_decode() {
        let mut w: Vec<u8> = vec![];
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

        let len = temi.encode(&mut w).unwrap();
        assert_eq!(len, temi.encoded_len());
        assert_eq!(len, 2 + 3 + 12 + 8 + 10 + 4 + 3);

        let decoded = TEMITimelineDescriptor::decode(&w).unwrap();
        assert_eq!(decoded, temi);
    }

    #[test]
    fn test_ntp_without_media_timestamp() {
        let mut w: Vec<u8> = vec![];
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

        let len = temi.encode(&mut w).unwrap();
        assert_eq!(len, temi.encoded_len());
        assert_eq!(len, 2 + 3 + 8 + 4 + 3);

        let decoded = TEMITimelineDescriptor::decode(&w).unwrap();
        assert_eq!(decoded, temi);
    }
}
