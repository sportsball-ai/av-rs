use super::{AudioEncoder, EncodedAudioPacket, RawAudioPacket};
use core::{
    pin::Pin,
    task::{Context, Poll},
};
use futures::{Sink, Stream};
use snafu::Snafu;

#[derive(Debug, Snafu)]
pub enum FdkAacEncoderError {}

pub struct FdkAacEncoder {}

impl Sink<RawAudioPacket> for FdkAacEncoder {
    type Error = FdkAacEncoderError;

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

impl Stream for FdkAacEncoder {
    type Item = Result<EncodedAudioPacket, FdkAacEncoderError>;

    fn poll_next(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        unimplemented!()
    }
}

impl AudioEncoder for FdkAacEncoder {
    type Error = FdkAacEncoderError;
}
