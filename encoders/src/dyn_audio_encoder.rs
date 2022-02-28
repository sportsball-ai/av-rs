use super::{AacConfig, AudioEncoder, EncodedAudioPacket, RawAudioPacket};
use snafu::Snafu;

/// A video encoder implemented by any of this crate's included implementations.
pub enum DynAudioEncoder {
    #[cfg(feature = "fdk_aac")]
    FdkAacEncoder(crate::fdk_aac_encoder::FdkAacEncoder),
}

#[derive(Debug, Snafu)]
pub enum DynAudioEncoderError {
    #[cfg(feature = "fdk_aac")]
    #[snafu(context(false), display("fdk aac encoder error"))]
    FdkAacEncoderError { source: crate::fdk_aac_encoder::FdkAacEncoderError },
    #[snafu(display("unsupported"))]
    Unsupported,
}

type Result<T> = core::result::Result<T, DynAudioEncoderError>;

#[derive(Clone, Debug)]
pub enum DynAudioEncoderConfig {
    Aac(AacConfig),
}

impl DynAudioEncoder {
    pub fn new(config: DynAudioEncoderConfig) -> Result<Self> {
        #[cfg(feature = "fdk_aac")]
        {
            let DynAudioEncoderConfig::Aac(config) = config;
            return Ok(Self::FdkAacEncoder(crate::fdk_aac_encoder::FdkAacEncoder::new(config)?));
        }

        #[cfg(not(feature = "fdk_aac"))]
        {
            let _ = config;
            Err(DynAudioEncoderError::Unsupported)
        }
    }
}

impl AudioEncoder for DynAudioEncoder {
    type Error = DynAudioEncoderError;

    fn encode(&mut self, packet: RawAudioPacket) -> Result<EncodedAudioPacket> {
        #[allow(unreachable_code)]
        Ok(match self {
            #[cfg(feature = "fdk_aac")]
            Self::FdkAacEncoder(e) => e.encode(packet)?,
            #[allow(unreachable_patterns)]
            _ => {
                let _ = packet;
                unreachable!()
            }
        })
    }
}
