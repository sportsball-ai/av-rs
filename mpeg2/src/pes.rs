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
    pub data_length: usize,
}

impl PacketHeader {
    pub fn decode(buf: &[u8]) -> Result<(Self, usize)> {
        if buf.len() < 6 {
            bail!("not enough bytes for pes header")
        }
        let stream_id = buf[3];
        let has_optional_header = match stream_id {
            0xbc | 0xbf | 0xf0 | 0xf1 | 0xff | 0xf2 | 0xf8 => false,
            _ => true,
        };
        if has_optional_header && buf.len() < 9 {
            bail!("not enough bytes for pes with optional header")
        }
        let packet_length = (buf[4] as usize) << 8 | (buf[5] as usize);
        let optional_header_length = match has_optional_header {
            true => 3 + buf[8] as usize,
            false => 0,
        };
        let data_offset = 6 + optional_header_length;
        if data_offset > buf.len() {
            bail!("not enough bytes for pes optional header length")
        }
        Ok((
            Self {
                stream_id,
                data_length: match packet_length {
                    0 => 0,
                    l @ _ => l - optional_header_length,
                },
            },
            data_offset,
        ))
    }
}

#[derive(Clone)]
pub struct Stream {
    pending: Option<Packet>,
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

                let (header, header_size) = PacketHeader::decode(&payload)?;
                self.pending = Some(Packet {
                    header,
                    data: payload[header_size..].to_vec(),
                })
            } else if let Some(pending) = &mut self.pending {
                pending.data.extend_from_slice(&payload);
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
