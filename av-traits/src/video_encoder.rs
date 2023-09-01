use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum VideoTimecodeMode {
    Normal,
    DropFrame,
}

/// A SMPTE video timecode.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct VideoTimecode {
    pub hours: u32,
    pub minutes: u32,
    pub seconds: u32,

    /// The number of frames within the second, between 0 and 29 inclusive.
    pub frames: u32,

    pub discontinuity: bool,
    pub mode: VideoTimecodeMode,
}

pub trait RawVideoFrame<S> {
    /// The samples that make up the frame's image. Typically this consists of 3 Y/U/V planes of
    /// `u8`s, but any format that the encoder supports can be used.
    fn samples(&self, plane: usize) -> &[S];

    /// If given, the video encoder may encode timecode information in the resulting bitstream. Not
    /// all codecs and encoders support this.
    fn timecode(&self) -> Option<&VideoTimecode> {
        None
    }
}

pub struct EncodedVideoFrame {
    pub data: Vec<u8>,
    pub is_keyframe: bool,
}

pub struct VideoEncoderOutput<F> {
    pub raw_frame: F,

    /// The encoded frame, or `None` if it was dropped by the encoder.
    pub encoded_frame: Option<EncodedVideoFrame>,
}

#[derive(Clone, Copy, Eq, PartialEq)]
pub enum EncodedFrameType {
    Auto,
    Key,
}

/// Implements basic video encoding behavior.
///
/// Typical usage should look like this:
///
/// ```
/// # use av_traits::{EncodedFrameType, RawVideoFrame, VideoEncoder};
/// fn encode<S, E>(mut source: S, mut encoder: E) -> Result<(), E::Error>
///     where S: Iterator<Item = Box<dyn RawVideoFrame<u8>>>,
///     E: VideoEncoder<RawVideoFrame = Box<dyn RawVideoFrame<u8>>>
/// {
///     while let Some(frame) = source.next() {
///         if let Some(output) = encoder.encode(frame, EncodedFrameType::Auto)? {
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
    fn encode(&mut self, frame: Self::RawVideoFrame, frame_type: EncodedFrameType) -> Result<Option<VideoEncoderOutput<Self::RawVideoFrame>>, Self::Error>;

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
