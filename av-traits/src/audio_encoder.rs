use alloc::vec::Vec;

pub struct RawAudioPacket<'a, S> {
    pub samples: &'a [S],
}

pub struct EncodedAudioPacket {
    pub data: Vec<u8>,
}

/// Implements basic audio encoding behavior.
///
/// Typical usage should look like this:
///
/// ```
/// # use av_traits::{RawAudioPacket, AudioEncoder};
/// fn encode<'a, S, E>(mut source: S, mut encoder: E) -> Result<(), E::Error>
///     where S: Iterator<Item = RawAudioPacket<'a, u16>>,
///     E: AudioEncoder<u16>
/// {
///     while let Some(packet) = source.next() {
///         let output = encoder.encode(packet)?;
///             // do something with output  
///     }
///
///     Ok(())
/// }
/// ```
pub trait AudioEncoder<S> {
    type Error;

    /// Encodes an audio packet.
    fn encode(&mut self, packet: RawAudioPacket<S>) -> Result<EncodedAudioPacket, Self::Error>;
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_audio_encoder_object_safety() {
        let _e: *const dyn AudioEncoder<u16, Error = ()>;
    }
}
