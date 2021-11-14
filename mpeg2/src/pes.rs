use super::ts;
use std::{
    error::Error,
    io::{self, Write},
};

pub type Result<T> = std::result::Result<T, Box<dyn Error + Send + Sync>>;

#[derive(Clone, Debug, PartialEq)]
pub struct Packet {
    pub header: PacketHeader,
    pub data: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct PacketHeader {
    pub stream_id: u8,
    pub optional_header: Option<OptionalHeader>,
    pub data_length: usize,
}

impl PacketHeader {
    pub fn decode(buf: &[u8]) -> Result<(Self, usize)> {
        if buf.len() < 6 {
            bail!("not enough bytes for pes header")
        }
        if buf[0] != 0 || buf[1] != 0 || buf[2] != 1 {
            bail!("incorrect start code for pes header")
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
            bail!("not enough bytes for pes optional header length")
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

    pub fn encode<W: Write>(&self, mut w: W) -> io::Result<usize> {
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

#[derive(Clone, Debug, PartialEq)]
pub struct OptionalHeader {
    pub data_alignment_indicator: bool,
    pub pts: Option<u64>,
    pub dts: Option<u64>,
}

const MAX_ENCODED_OPTIONAL_HEADER_LENGTH: usize = 13;

impl OptionalHeader {
    pub fn decode(buf: &[u8]) -> Result<(Self, usize)> {
        if buf.len() < 3 {
            bail!("not enough bytes for pes optional header")
        }
        let len = 3 + buf[2] as usize;
        let mut offset = 3;
        let pts = if buf[1] & 0x80 != 0 {
            if buf.len() < offset + 5 {
                bail!("not enough bytes for pes pts")
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
                bail!("not enough bytes for pes dts")
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

    pub fn encode<W: Write>(&self, mut w: W) -> io::Result<usize> {
        let mut buf = [0u8; MAX_ENCODED_OPTIONAL_HEADER_LENGTH];
        let mut len = 3;
        buf[0] = 0x80; // marker

        if self.data_alignment_indicator {
            buf[0] |= 0b100;
        }

        if let Some(pts) = self.pts {
            buf[1] |= 0x80;
            buf[3] = 0b00100001 | (pts >> 30) as u8;
            buf[4] = (pts >> 22) as u8;
            buf[5] = (pts >> 14) as u8 | 1;
            buf[6] = (pts >> 7) as u8;
            buf[7] = (pts << 1) as u8 | 1;
            len = 8;
        }

        if let Some(dts) = self.dts {
            buf[1] |= 0x40;
            buf[3] |= 0b00010000;
            buf[8] = 0b00010001 | (dts >> 30) as u8;
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
    pending: Option<Packet>,
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
    pub fn write(&mut self, packet: &ts::Packet) -> Result<Vec<Packet>> {
        let mut completed = Vec::new();

        if let Some(payload) = packet.payload {
            if packet.payload_unit_start_indicator {
                if let Some(pending) = self.pending.take() {
                    if pending.header.data_length == 0 {
                        completed.push(pending);
                    }
                }

                let (header, header_size) = PacketHeader::decode(payload)?;
                self.pending = Some(Packet {
                    header,
                    data: payload[header_size..].to_vec(),
                })
            } else if let Some(pending) = &mut self.pending {
                pending.data.extend_from_slice(payload);
            }
        }

        if match &self.pending {
            Some(pending) => pending.header.data_length > 0 && pending.data.len() >= pending.header.data_length,
            None => false,
        } {
            if let Some(mut pending) = self.pending.take() {
                if pending.header.data_length > 0 {
                    pending.data.truncate(pending.header.data_length);
                }
                completed.push(pending);
            }
        }

        Ok(completed)
    }

    pub fn flush(&mut self) -> Result<Vec<Packet>> {
        let mut completed = Vec::new();

        if let Some(pending) = self.pending.take() {
            if pending.header.data_length == 0 {
                completed.push(pending);
            }
        }

        Ok(completed)
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
    }
}
