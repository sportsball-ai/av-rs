use futures::{Sink, Stream};

pub struct RawAudioPacket {}

pub struct EncodedAudioPacket {}

pub trait AudioEncoder:
    Sink<RawAudioPacket, Error = <Self as AudioEncoder>::Error> + Stream<Item = Result<EncodedAudioPacket, <Self as AudioEncoder>::Error>>
{
    type Error;
}
