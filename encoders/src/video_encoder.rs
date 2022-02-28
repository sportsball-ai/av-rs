use futures::{Sink, Stream};

pub struct RawVideoFrame {}

pub struct EncodedVideoFrame {}

pub trait VideoEncoder:
    Sink<RawVideoFrame, Error = <Self as VideoEncoder>::Error> + Stream<Item = Result<EncodedVideoFrame, <Self as VideoEncoder>::Error>>
{
    type Error;
}
