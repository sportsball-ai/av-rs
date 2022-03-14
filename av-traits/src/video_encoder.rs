use alloc::vec::Vec;

pub trait RawVideoFrame<S> {
    /// The samples that make up the frame's image. Typically this consists of 3 Y/U/V planes of
    /// `u8`s, but any format that the encoder supports can be used.
    fn samples(&self, plane: usize) -> &[S];
}

pub struct EncodedVideoFrame {
    pub data: Vec<u8>,
    pub is_keyframe: bool,
}

pub struct VideoEncoderOutput<F> {
    pub raw_frame: F,
    pub encoded_frame: EncodedVideoFrame,
}

/// Implements basic video encoding behavior.
///
/// Typical usage should look like this:
///
/// ```
/// # use av_traits::{RawVideoFrame, VideoEncoder};
/// fn encode<S, E>(mut source: S, mut encoder: E) -> Result<(), E::Error>
///     where S: Iterator<Item = Box<dyn RawVideoFrame<u8>>>,
///     E: VideoEncoder<RawVideoFrame = Box<dyn RawVideoFrame<u8>>>
/// {
///     while let Some(frame) = source.next() {
///         if let Some(output) = encoder.encode(frame)? {
///             // do something with output  
///         }
///     }
///
///     while let Some(output) = encoder.flush()? {
///         // do something with output  
///     }
///
///     Ok(())
/// }
/// ```
pub trait VideoEncoder {
    type Error;
    type RawVideoFrame;

    /// Sends a frame to the encoder. This may block while the encoder performs encoding.
    ///
    /// `None` may be returned at the start of a session to allow for delayed encoder output (e.g.
    /// for B-frames or lookahead RC).
    ///
    /// Because output may be delayed, the returned frame is not necessarily the same as the input
    /// frame.
    fn encode(&mut self, frame: Self::RawVideoFrame) -> Result<Option<VideoEncoderOutput<Self::RawVideoFrame>>, Self::Error>;

    /// Indicates to the encoder that no more input will be provided and it should emit any delayed
    /// frames. This should be invoked until no more frames are returned.
    fn flush(&mut self) -> Result<Option<VideoEncoderOutput<Self::RawVideoFrame>>, Self::Error>;
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_video_encoder_object_safety() {
        let _e: *const dyn VideoEncoder<Error = (), RawVideoFrame = ()>;
    }
}
