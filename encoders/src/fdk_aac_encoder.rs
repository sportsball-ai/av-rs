use super::{AacConfig, AudioEncoder, EncodedAudioPacket, RawAudioPacket};
use snafu::Snafu;

#[derive(Debug, Snafu)]
pub enum FdkAacEncoderError {}

type Result<T> = core::result::Result<T, FdkAacEncoderError>;

pub struct FdkAacEncoder {}

impl FdkAacEncoder {
    pub fn new(_config: AacConfig) -> Result<Self> {
        Ok(Self {})
    }
}

impl AudioEncoder for FdkAacEncoder {
    type Error = FdkAacEncoderError;

    fn encode(&mut self, _packet: RawAudioPacket) -> Result<EncodedAudioPacket> {
        unimplemented!()
    }
}
