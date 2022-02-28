use super::{AvcConfig, EncodedVideoFrame, RawVideoFrame, VideoEncoder};
use snafu::Snafu;

/// A video encoder implemented by any of this crate's included implementations.
pub enum DynVideoEncoder {
    #[cfg(feature = "x264")]
    X264Encoder(crate::x264_encoder::X264Encoder),
}

#[derive(Debug, Snafu)]
pub enum DynVideoEncoderError {
    #[cfg(feature = "x264")]
    #[snafu(context(false), display("x264 encoder error"))]
    X264EncoderError { source: crate::x264_encoder::X264EncoderError },
    #[snafu(display("unsupported"))]
    Unsupported,
}

type Result<T> = core::result::Result<T, DynVideoEncoderError>;

#[derive(Clone, Debug)]
pub enum DynVideoEncoderConfig {
    Avc(AvcConfig),
}

impl DynVideoEncoder {
    /// Creates a new encoder using the best available implementation for the given configuration.
    pub fn new(config: DynVideoEncoderConfig) -> Result<Self> {
        #[cfg(feature = "x264")]
        {
            let DynVideoEncoderConfig::Avc(config) = config;
            return Ok(Self::X264Encoder(crate::x264_encoder::X264Encoder::new(config)?));
        }

        #[allow(unreachable_code)]
        {
            let _ = config;
            Err(DynVideoEncoderError::Unsupported)
        }
    }
}

impl VideoEncoder for DynVideoEncoder {
    type Error = DynVideoEncoderError;

    fn encode(&mut self, frame: RawVideoFrame) -> Result<Option<EncodedVideoFrame>> {
        #[allow(unreachable_code)]
        Ok(match self {
            #[cfg(feature = "x264")]
            Self::X264Encoder(e) => e.encode(frame)?,
            #[allow(unreachable_patterns)]
            _ => {
                let _ = frame;
                unreachable!()
            }
        })
    }

    fn flush(&mut self) -> Result<Option<EncodedVideoFrame>> {
        #[allow(unreachable_code)]
        Ok(match self {
            #[cfg(feature = "x264")]
            Self::X264Encoder(e) => e.flush()?,
            #[allow(unreachable_patterns)]
            _ => unreachable!(),
        })
    }
}
