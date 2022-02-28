use super::{EncodedVideoFrame, RawVideoFrame, VideoEncoder};
use core::{
    pin::Pin,
    task::{Context, Poll},
};
use futures::{Sink, Stream};
use snafu::Snafu;

/// A video encoder implemented by any of this crate's included implementations.
pub enum DynVideoEncoder {
    #[cfg(feature = "x264")]
    X264Encoder(crate::x264_encoder::X264Encoder),
}

#[derive(Debug, Snafu)]
pub enum DynVideoEncoderError {
    #[cfg(feature = "x264")]
    X264EncoderError { source: crate::x264_encoder::X264EncoderError },
}

#[derive(Clone, Debug)]
pub enum DynVideoEncoderConfig {
    Avc {},
}

impl DynVideoEncoder {
    pub fn new(_config: DynVideoEncoderConfig) -> Self {
        unimplemented!()
    }
}

impl Sink<RawVideoFrame> for DynVideoEncoder {
    type Error = DynVideoEncoderError;

    fn poll_ready(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        unimplemented!()
    }

    fn start_send(self: Pin<&mut Self>, _item: RawVideoFrame) -> Result<(), Self::Error> {
        unimplemented!()
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        unimplemented!()
    }

    fn poll_close(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        unimplemented!()
    }
}

impl Stream for DynVideoEncoder {
    type Item = Result<EncodedVideoFrame, DynVideoEncoderError>;

    fn poll_next(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        unimplemented!()
    }
}

impl VideoEncoder for DynVideoEncoder {
    type Error = DynVideoEncoderError;
}
