use super::{ts, DecodeError, EncodeError};
use crate::muxer;
use crate::temi::TEMITimelineDescriptor;
use alloc::{borrow::Cow, vec::Vec};
use core::mem;
use core2::io::Write;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Packet<'a> {
    pub header: PacketHeader,
    pub data: Cow<'a, [u8]>,
}

#[derive(Clone, Debug, Default)]
pub struct PacketizationConfig<'a> {
    pub packet_id: u16,
    pub random_access_indicator: bool,
    pub continuity_counter: u8,
    pub temi_timeline_descriptors: Vec<TEMITimelineDescriptor>,
    pub private_data_bytes: &'a [u8],
}

impl<'a> Packet<'a> {
    pub fn packetize(&self, config: PacketizationConfig<'a>) -> Packetize {
        Packetize {
            header: Some(&self.header),
            data: &self.data,
            config,
        }
    }
}

pub struct Packetize<'a> {
    header: Option<&'a PacketHeader>,
    data: &'a [u8],
    config: PacketizationConfig<'a>,
}

impl<'a> Iterator for Packetize<'a> {
    type Item = ts::Packet<'a>;

    fn next(&mut self) -> Option<Self::Item> {
        let adaptation_field = self.header.map(|header| {
            let mut af = ts::AdaptationField {
                random_access_indicator: if self.config.random_access_indicator { Some(true) } else { None },
                temi_timeline_descriptors: mem::take(&mut self.config.temi_timeline_descriptors),
                private_data_bytes: Cow::Borrowed(self.config.private_data_bytes),
                ..Default::default()
            };
            if let Some(dts) = header.optional_header.as_ref().and_then(|h| h.dts.or(h.pts)) {
                af.program_clock_reference_27mhz = Some(dts * 300);
            }
            af
        });

        if adaptation_field.is_none() && self.data.is_empty() {
            None
        } else {
            let max_payload_len = ts::Packet::max_payload_len(adaptation_field.as_ref());

            let mut data_consumed = max_payload_len.min(self.data.len());
            let mut payload = Cow::Borrowed(&self.data[..data_consumed]);
            if let Some(header) = self.header.take() {
                let mut buffer = Vec::with_capacity(ts::PACKET_LENGTH);
                let header_len = header.encode(&mut buffer).expect("encoding to the buffer should never fail");
                data_consumed -= header_len.min(data_consumed);
                buffer.extend_from_slice(&self.data[..data_consumed]);
                payload = buffer.into();
            }

            let p = ts::Packet {
                packet_id: self.config.packet_id,
                payload_unit_start_indicator: adaptation_field.is_some(),
                adaptation_field: adaptation_field.or_else(|| if payload.len() < max_payload_len { Some(Default::default()) } else { None }),
                continuity_counter: self.config.continuity_counter,
                payload: if !payload.is_empty() { Some(payload) } else { None },
            };
            if p.payload.is_some() {
                self.config.continuity_counter = (self.config.continuity_counter + 1) % 16;
                self.data = &self.data[data_consumed..];
            }
            Some(p)
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PacketHeader {
    pub stream_id: u8,
    pub optional_header: Option<OptionalHeader>,
    pub data_length: usize,
}

impl PacketHeader {
    pub fn new(stream: &muxer::Stream, p: &muxer::Packet) -> Self {
        Self {
            stream_id: stream.stream_id(),
            optional_header: Some(OptionalHeader {
                data_alignment_indicator: true,
                pts: p.pts_90khz,
                dts: p.dts_90khz,
            }),
            data_length: if stream.unbounded_data_length() { 0 } else { p.data.len() },
        }
    }

    pub fn decode(buf: &[u8]) -> Result<(Self, usize), DecodeError> {
        if buf.len() < 6 {
            return Err(DecodeError::new("not enough bytes for pes header"));
        }
        if buf[0] != 0 || buf[1] != 0 || buf[2] != 1 {
            return Err(DecodeError::new("incorrect start code for pes header"));
        }
        let stream_id = buf[3];
        let (optional_header, optional_header_length) = match stream_id {
            0xc0..=0xef => {
                let decoded = OptionalHeader::decode(&buf[6..])?;
                (Some(decoded.0), decoded.1)
            }
            _ => (None, 0),
        };
        let packet_length = (buf[4] as usize) << 8 | (buf[5] as usize);
        let data_offset = 6 + optional_header_length;
        if data_offset > buf.len() {
            return Err(DecodeError::new("not enough bytes for pes optional header length"));
        }
        Ok((
            Self {
                stream_id,
                optional_header,
                data_length: match packet_length {
                    0 => 0,
                    l => l - optional_header_length,
                },
            },
            data_offset,
        ))
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        let mut buf = [0u8; 6 + MAX_ENCODED_OPTIONAL_HEADER_LENGTH];
        buf[2] = 1; // start code prefix
        buf[3] = self.stream_id;
        let optional_header_length = match &self.optional_header {
            Some(h) => h.encode(&mut buf[6..])?,
            None => 0,
        };
        if self.data_length > 0 {
            let packet_length = optional_header_length + self.data_length;
            buf[4] = (packet_length >> 8) as u8;
            buf[5] = packet_length as u8;
        }
        let len = 6 + optional_header_length;
        w.write_all(&buf[..len])?;
        Ok(len)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OptionalHeader {
    pub data_alignment_indicator: bool,
    pub pts: Option<u64>,
    pub dts: Option<u64>,
}

const MAX_ENCODED_OPTIONAL_HEADER_LENGTH: usize = 13;

impl OptionalHeader {
    pub fn decode(buf: &[u8]) -> Result<(Self, usize), DecodeError> {
        if buf.len() < 3 {
            return Err(DecodeError::new("not enough bytes for pes optional header"));
        }
        let len = 3 + buf[2] as usize;
        let mut offset = 3;
        let pts = if buf[1] & 0x80 != 0 {
            if buf.len() < offset + 5 {
                return Err(DecodeError::new("not enough bytes for pes pts"));
            }
            let pts = ((buf[offset] as u64 >> 1) & 7) << 30
                | (buf[offset + 1] as u64) << 22
                | (buf[offset + 2] as u64 >> 1) << 15
                | (buf[offset + 3] as u64) << 7
                | (buf[offset + 4] as u64 >> 1);
            offset += 5;
            Some(pts)
        } else {
            None
        };
        let dts = if buf[1] & 0x40 != 0 {
            if buf.len() < offset + 5 {
                return Err(DecodeError::new("not enough bytes for pes dts"));
            }
            let dts = ((buf[offset] as u64 >> 1) & 7) << 30
                | (buf[offset + 1] as u64) << 22
                | (buf[offset + 2] as u64 >> 1) << 15
                | (buf[offset + 3] as u64) << 7
                | (buf[offset + 4] as u64 >> 1);
            Some(dts)
        } else {
            None
        };
        Ok((
            Self {
                data_alignment_indicator: (buf[0] & 4) != 0,
                pts,
                dts,
            },
            len,
        ))
    }

    pub fn encode<W: Write>(&self, mut w: W) -> Result<usize, EncodeError> {
        let mut buf = [0u8; MAX_ENCODED_OPTIONAL_HEADER_LENGTH];
        let mut len = 3;
        buf[0] = 0x80; // marker

        if self.data_alignment_indicator {
            buf[0] |= 0b100;
        }

        if let Some(pts) = self.pts {
            buf[1] |= 0x80;
            buf[3] = 0b00100001 | ((pts >> 29) & 0xf) as u8;
            buf[4] = (pts >> 22) as u8;
            buf[5] = (pts >> 14) as u8 | 1;
            buf[6] = (pts >> 7) as u8;
            buf[7] = (pts << 1) as u8 | 1;
            len = 8;
        }

        if let Some(dts) = self.dts {
            buf[1] |= 0x40;
            buf[3] |= 0b00010000;
            buf[8] = 0b00010001 | ((dts >> 29) & 0xf) as u8;
            buf[9] = (dts >> 22) as u8;
            buf[10] = (dts >> 14) as u8 | 1;
            buf[11] = (dts >> 7) as u8;
            buf[12] = (dts << 1) as u8 | 1;
            len = 13;
        }

        buf[2] = len - 3;
        w.write_all(&buf[..len as usize])?;
        Ok(len as _)
    }
}

#[derive(Clone)]
pub struct Stream {
    pending: Option<Packet<'static>>,
}

impl Default for Stream {
    fn default() -> Self {
        Self::new()
    }
}

impl Stream {
    pub fn new() -> Self {
        Self { pending: None }
    }

    /// Writes transport packets to the stream. Whenever a PES packet is completed, it is returned.
    pub fn write(&mut self, packet: &ts::Packet) -> Result<Vec<Packet<'static>>, DecodeError> {
        let mut completed = Vec::new();

        if let Some(payload) = &packet.payload {
            if packet.payload_unit_start_indicator {
                if let Some(pending) = self.pending.take() {
                    if pending.header.data_length == 0 {
                        completed.push(pending);
                    }
                }

                let (header, header_size) = PacketHeader::decode(payload)?;
                self.pending = Some(Packet {
                    header,
                    data: payload[header_size..].to_vec().into(),
                })
            } else if let Some(pending) = &mut self.pending {
                pending.data.to_mut().extend_from_slice(payload);
            }
        }

        if match &self.pending {
            Some(pending) => pending.header.data_length > 0 && pending.data.len() >= pending.header.data_length,
            None => false,
        } {
            if let Some(mut pending) = self.pending.take() {
                if pending.header.data_length > 0 {
                    pending.data.to_mut().truncate(pending.header.data_length);
                }
                completed.push(pending);
            }
        }

        Ok(completed)
    }

    pub fn flush(&mut self) -> Vec<Packet<'static>> {
        let mut completed = Vec::new();

        if let Some(pending) = self.pending.take() {
            if pending.header.data_length == 0 {
                completed.push(pending);
            }
        }

        completed
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_packet_header_decode_encode() {
        let buf = vec![
            0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x80, 0xC0, 0x0A, 0x31, 0x00, 0x07, 0xEF, 0xD7, 0x11, 0x00, 0x07, 0xD8, 0x61,
        ];

        let (header, n) = PacketHeader::decode(&buf).unwrap();
        assert_eq!(n, buf.len());
        assert_eq!(
            header,
            PacketHeader {
                stream_id: 0xe0,
                optional_header: Some(OptionalHeader {
                    data_alignment_indicator: false,
                    pts: Some(129_003),
                    dts: Some(126_000),
                }),
                data_length: 0,
            }
        );

        let mut encoded = vec![];
        header.encode(&mut encoded).unwrap();
        assert_eq!(buf, encoded);

        let ts = (0b111 << 30) + (0x5f375a86 & ((1 << 30) - 1));
        let oh = OptionalHeader {
            data_alignment_indicator: true,
            pts: Some(ts),
            dts: Some(ts),
        };
        let mut encoded_oh = vec![];
        oh.encode(&mut encoded_oh).unwrap();
        let (decoded_oh, _) = OptionalHeader::decode(&encoded_oh).unwrap();
        assert!(decoded_oh.data_alignment_indicator);
        assert_eq!(decoded_oh.pts.unwrap(), ts);
        assert_eq!(decoded_oh.dts.unwrap(), ts);
    }

    /// Tests that if timestamps using more than 33 bits are given, the high bits are dropped and
    /// don't corrupt the header.
    #[test]
    fn test_packet_header_ts_overflow() {
        let header = OptionalHeader {
            data_alignment_indicator: false,
            pts: Some((1 << 35) | 123),
            dts: Some((1 << 35) | 456),
        };

        let mut encoded = vec![];
        header.encode(&mut encoded).unwrap();

        assert_eq!(encoded[3], 0b00110001);
        assert_eq!(encoded[8], 0b00010001);
    }
}
