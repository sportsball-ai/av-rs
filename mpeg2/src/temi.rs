use crate::{DecodeError, EncodeError};
use byteorder::{BigEndian, ByteOrder};
use core2::io::Write;

const AF_DESCR_TAG_TIMELINE: u8 = 0x04;

#[derive(Debug, Default, PartialEq)]
pub struct TEMITimelineDescriptor {
    pub timescale: u32,
    pub media_timestamp: Option<u64>,

    pub ntp_timestamp: Option<u64>,
    pub ptp_timestamp: Option<u128>,

    pub drop: bool,
    pub frames_per_tc_seconds: u16,
    pub duration: u16,
    pub time_code: Option<u64>,

    pub force_reload: bool,
    pub paused: bool,
    pub discontinuity: bool,
    pub timeline_id: u8,
}

impl TEMITimelineDescriptor {
    pub fn encode_len(&self) -> usize {
        let mut len = 5;
        len += self.media_timestamp.map(|ts| if ts >> 32 == 0 { 8 } else { 12 }).unwrap_or(0);
        if self.ntp_timestamp.is_some() {
            len += 8;
        }
        if self.ptp_timestamp.is_some() {
            len += 10;
        }

        len += self.time_code.map(|tc| if tc >> 24 == 0 { 7 } else { 12 }).unwrap_or(0);
        len
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        let mut buf = vec![0u8; self.encode_len()];
        buf[0] = AF_DESCR_TAG_TIMELINE;
        let has_timestamp = self.media_timestamp.map(|ts| if ts >> 32 != 0 { 2 } else { 1 }).unwrap_or(0);
        buf[2] |= has_timestamp << 6;

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
        if let Some(media_timestamp) = self.media_timestamp {
            BigEndian::write_u32(&mut buf[5..=8], self.timescale);
            BigEndian::write_u32(&mut buf[9..=12], media_timestamp as u32);
            ret += 8;
            if has_timestamp == 2 {
                BigEndian::write_u32(&mut buf[13..=16], (media_timestamp >> 32) as u32);
                ret += 4;
            } else if has_timestamp != 1 {
                return Err(EncodeError::other("Invalid has_timestamp value"));
            }
        }

        if let Some(ntp_ts) = self.ntp_timestamp {
            buf[2] |= 0b0010_0000;
            BigEndian::write_u64(&mut buf[ret..(ret + 8)], ntp_ts);
            ret += 8;
        }
        if let Some(ptp_ts) = self.ptp_timestamp {
            buf[2] |= 0b0001_0000;
            BigEndian::write_u64(&mut buf[ret..(ret + 8)], ptp_ts as u64);
            ret += 8;
            BigEndian::write_u16(&mut buf[ret..(ret + 2)], (ptp_ts >> 64) as u16);
            ret += 2;
        }

        if let Some(time_code) = self.time_code {
            let has_timecode: u8 = if time_code >> 24 == 0 { 1 } else { 2 };
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
                buf[ret + i] = (time_code >> (i * 8)) as u8;
            }
            ret += 3;
            if has_timecode == 2 {
                for i in 0..5 {
                    buf[ret + i] = (time_code >> ((i + 3) * 8)) as u8;
                }
                ret += 5;
            } else if has_timecode != 1 {
                return Err(EncodeError::other("Invalid has_timecode value"));
            }
        }

        buf[1] = ret as u8 - 2;

        w.write_all(&buf)?;
        Ok(buf.len())
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

        let has_timestamp = buf[2] >> 6;

        let mut n = 5;
        if has_timestamp != 0 {
            ret.timescale = BigEndian::read_u32(&buf[5..=8]);
            let mut media_timestamp;
            if has_timestamp <= 2 {
                media_timestamp = BigEndian::read_u32(&buf[9..=12]) as u64;
                n += 8;
            } else {
                return Err(DecodeError::new("incorrect media_timestamp for temi_timeline_descriptor"));
            }
            if has_timestamp == 2 {
                n += 4;
                media_timestamp |= (BigEndian::read_u32(&buf[13..=24]) as u64) << 32;
            }
            ret.media_timestamp = Some(media_timestamp);
        }

        if buf[2] & 0b0010_0000 != 0 {
            let ts = BigEndian::read_u64(&buf[n..(n + 8)]);
            n += 8;
            ret.ntp_timestamp = Some(ts);
        }
        if buf[2] & 0b0001_0000 != 0 {
            let mut ts = BigEndian::read_u64(&buf[n..(n + 8)]) as u128;
            n += 8;
            ts |= (BigEndian::read_u16(&buf[n..(n + 2)]) as u128) << 64;
            n += 2;
            ret.ptp_timestamp = Some(ts);
        }

        let has_timecode = (buf[2] & 0b0000_1100) >> 2;
        if has_timecode != 0 {
            ret.drop = buf[n] >> 7 == 1;
            ret.frames_per_tc_seconds = (buf[n] as u16 & 0b0111_1111) << 8 | buf[n + 1] as u16;
            ret.duration = (buf[n + 2] as u16) << 8 | buf[n + 3] as u16;
            n += 4;

            let mut time_code;
            if has_timecode <= 2 {
                time_code = buf[n] as u64 | (buf[n + 1] as u64) << 8 | (buf[n + 2] as u64) << 16;
                if has_timecode == 2 {
                    n += 3;
                    time_code |=
                        (buf[n] as u64 | (buf[n + 1] as u64) << 8 | (buf[n + 2] as u64) << 16 | (buf[n + 3] as u64) << 24 | (buf[n + 4] as u64) << 32) << 24;
                }
            } else {
                return Err(DecodeError::new("incorrect time_code for temi_timeline_descriptor"));
            }

            ret.time_code = Some(time_code);
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
    fn test_encode_decode() {
        let mut w: Vec<u8> = vec![];
        let temi = TEMITimelineDescriptor {
            timescale: 0xEBF1_3405,
            media_timestamp: Some(0xa1dd_3e6a_19d7_bfec),

            ntp_timestamp: Some(0xfdc9_b5f8_25e9_9236),
            ptp_timestamp: Some(0xcdf8_fdc9_b5f8_25e9_9236),

            drop: true,
            frames_per_tc_seconds: 0x75a3,
            duration: 0x988a,
            time_code: Some(0xab_82ef),

            force_reload: true,
            paused: true,
            discontinuity: true,
            timeline_id: 0xf3,
        };

        let len = temi.encode(&mut w).unwrap();
        assert_eq!(len, temi.encode_len());
        assert_eq!(len, 2 + 3 + 12 + 8 + 10 + 4 + 3);

        let decoded = TEMITimelineDescriptor::decode(&w).unwrap();
        assert_eq!(decoded, temi);
    }

    #[test]
    fn test_ntp_without_media_timestamp() {
        let mut w: Vec<u8> = vec![];
        let temi = TEMITimelineDescriptor {
            timescale: 0,
            media_timestamp: None,

            ntp_timestamp: Some(0xfdc9_b5f8_25e9_9236),
            ptp_timestamp: None,

            drop: true,
            frames_per_tc_seconds: 0x75a3,
            duration: 0x988a,
            time_code: Some(0xab_82ef),

            force_reload: true,
            paused: true,
            discontinuity: false,
            timeline_id: 0xf3,
        };

        let len = temi.encode(&mut w).unwrap();
        assert_eq!(len, temi.encode_len());
        assert_eq!(len, 2 + 3 + 8 + 4 + 3);

        let decoded = TEMITimelineDescriptor::decode(&w).unwrap();
        assert_eq!(decoded, temi);
    }
}
