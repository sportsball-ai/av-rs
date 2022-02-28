pub struct RawVideoFrame {}

pub struct EncodedVideoFrame {}

#[derive(Clone, Debug)]
pub struct AvcConfig {}

/// Implements basic video encoding behavior. Typical usage should look like this:
///
/// ```
/// # use encoders::{RawVideoFrame, VideoEncoder};
/// fn encode<S, E: VideoEncoder>(mut source: S, mut encoder: E) -> Result<(), E::Error>
///     where S: Iterator<Item = RawVideoFrame>,
/// {
///     while let Some(raw_frame) = source.next() {
///         if let Some(encoded_frame) = encoder.encode(raw_frame)? {
///             // do something with encoded_frame
///         }
///     }
///
///     while let Some(encoded_frame) = encoder.flush()? {
///         // do something with encoded_frame
///     }
///
///     Ok(())
/// }
/// ```
pub trait VideoEncoder {
    type Error;

    /// Sends a frame to the encoder. This may block while the encoder performs encoding.
    ///
    /// `None` may be returned at the start of a session to allow for delayed encoder output (e.g.
    /// for B-frames or lookahead RC).
    ///
    /// Because output may be delayed, the returned frame is not necessarily the same as the input
    /// frame.
    fn encode(&mut self, frame: RawVideoFrame) -> Result<Option<EncodedVideoFrame>, Self::Error>;

    /// Indicates to the encoder that no more input will be provided and it should emit any delayed
    /// frames. This should be invoked until no more frames are returned.
    fn flush(&mut self) -> Result<Option<EncodedVideoFrame>, Self::Error>;
}
