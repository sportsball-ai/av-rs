use super::{EncodedVideoFrame, RawVideoFrame, VideoEncoder};
use core::{
    pin::Pin,
    task::{Context, Poll},
};
use futures::{Sink, Stream};
use snafu::Snafu;

#[link(name = "x264")]
extern "C" {
    // TODO
}

#[derive(Debug, Snafu)]
pub enum X264EncoderError {}

pub struct X264Encoder {}

impl Sink<RawVideoFrame> for X264Encoder {
    type Error = X264EncoderError;

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

impl Stream for X264Encoder {
    type Item = Result<EncodedVideoFrame, X264EncoderError>;

    fn poll_next(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        unimplemented!()
    }
}

impl VideoEncoder for X264Encoder {
    type Error = X264EncoderError;
}
