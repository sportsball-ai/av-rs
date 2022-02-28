pub struct RawAudioPacket {}

pub struct EncodedAudioPacket {}

#[derive(Clone, Debug)]
pub struct AacConfig {}

/// Implements basic audio encoding behavior. Typical usage should look like this:
///
/// ```
/// # use encoders::{RawAudioPacket, AudioEncoder};
/// fn encode<S, E: AudioEncoder>(mut source: S, mut encoder: E) -> Result<(), E::Error>
///     where S: Iterator<Item = RawAudioPacket>,
/// {
///     while let Some(raw_packet) = source.next() {
///         let encoded_packet = encoder.encode(raw_packet)?;
///         // do something with encoded_packet
///     }
///
///     Ok(())
/// }
/// ```
pub trait AudioEncoder {
    type Error;

    /// Sends a frame to the encoder. This may block while the encoder performs encoding.
    fn encode(&mut self, packet: RawAudioPacket) -> Result<EncodedAudioPacket, Self::Error>;
}
