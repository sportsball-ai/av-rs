use super::{AudioEncoder, EncodedAudioPacket, RawAudioPacket};
use core::{
    pin::Pin,
    task::{Context, Poll},
};
use futures::{Sink, Stream};
use snafu::Snafu;

/// A video encoder implemented by any of this crate's included implementations.
pub enum DynAudioEncoder {
    #[cfg(feature = "fdk_aac")]
    FdkAacEncoder(crate::fdk_aac_encoder::FdkAacEncoder),
}

#[derive(Debug, Snafu)]
pub enum DynAudioEncoderError {
    #[cfg(feature = "fdk_aac")]
    FdkAacEncoderError { source: crate::fdk_aac_encoder::FdkAacEncoderError },
}

#[derive(Clone, Debug)]
pub enum DynAudioEncoderConfig {
    Aac {},
}

impl DynAudioEncoder {
    pub fn new(_config: DynAudioEncoderConfig) -> Self {
        unimplemented!()
    }
}

impl Sink<RawAudioPacket> for DynAudioEncoder {
    type Error = DynAudioEncoderError;

    fn poll_ready(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        unimplemented!()
    }

    fn start_send(self: Pin<&mut Self>, _item: RawAudioPacket) -> Result<(), Self::Error> {
        unimplemented!()
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        unimplemented!()
    }

    fn poll_close(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Result<(), Self::Error>> {
        unimplemented!()
    }
}

impl Stream for DynAudioEncoder {
    type Item = Result<EncodedAudioPacket, DynAudioEncoderError>;

    fn poll_next(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        unimplemented!()
    }
}

impl AudioEncoder for DynAudioEncoder {
    type Error = DynAudioEncoderError;
}
