use alloc::vec::Vec;

pub trait RawVideoFrame<S> {
    /// The samples that make up the frame's image. Typically this consists of 3 Y/U/V planes of
    /// `u8`s, but any format that the encoder supports can be used.
    fn samples(&self, plane: usize) -> &[S];
}

pub struct EncodedVideoFrame {
    pub data: Vec<u8>,
}

pub struct VideoEncoderInput<F, C> {
    pub frame: F,
    pub context: C,
}

pub struct VideoEncoderOutput<C> {
    pub frame: EncodedVideoFrame,
    pub context: C,
}

/// Implements basic video encoding behavior. Arbitrary context associated with each frame can be
/// passed through video encoders.
///
/// Typical usage should look like this:
///
/// ```
/// # use av_traits::{RawVideoFrame, VideoEncoder, VideoEncoderInput};
/// fn encode<S, E>(mut source: S, mut encoder: E) -> Result<(), E::Error>
///     where S: Iterator<Item = Box<dyn RawVideoFrame<u8>>>,
///     E: VideoEncoder<Context = (), RawVideoFrame = Box<dyn RawVideoFrame<u8>>>
/// {
///     while let Some(frame) = source.next() {
///         if let Some(encoded_frame) = encoder.encode(VideoEncoderInput{frame, context: ()})? {
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
    type Context;
    type Error;
    type RawVideoFrame;

    /// Sends a frame to the encoder. This may block while the encoder performs encoding.
    ///
    /// `None` may be returned at the start of a session to allow for delayed encoder output (e.g.
    /// for B-frames or lookahead RC).
    ///
    /// Because output may be delayed, the returned frame is not necessarily the same as the input
    /// frame.
    fn encode(&mut self, frame: VideoEncoderInput<Self::RawVideoFrame, Self::Context>) -> Result<Option<VideoEncoderOutput<Self::Context>>, Self::Error>;

    /// Indicates to the encoder that no more input will be provided and it should emit any delayed
    /// frames. This should be invoked until no more frames are returned.
    fn flush(&mut self) -> Result<Option<VideoEncoderOutput<Self::Context>>, Self::Error>;
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_video_encoder_object_safety() {
        let _e: *const dyn VideoEncoder<Context = (), Error = (), RawVideoFrame = ()>;
    }
}
