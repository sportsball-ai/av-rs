use super::ts;

use std::error::Error;

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
}

#[derive(Clone, Debug, PartialEq)]
pub struct OptionalHeader {
    pub pts: Option<u64>,
    pub dts: Option<u64>,
}

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
        Ok((Self { pts, dts }, len))
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
    fn test_packet_header_decode() {
        let buf = &[
            0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x80, 0xC0, 0x0A, 0x31, 0x00, 0x07, 0xEF, 0xD7, 0x11, 0x00, 0x07, 0xD8, 0x61,
        ];

        let (header, n) = PacketHeader::decode(buf).unwrap();
        assert_eq!(n, buf.len());
        assert_eq!(
            header,
            PacketHeader {
                stream_id: 0xe0,
                optional_header: Some(OptionalHeader {
                    pts: Some(129_003),
                    dts: Some(126_000),
                }),
                data_length: 0,
            }
        );
    }
}
