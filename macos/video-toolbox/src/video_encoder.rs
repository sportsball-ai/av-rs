use super::*;
use av_traits::{EncodedFrameType, EncodedVideoFrame, RawVideoFrame, VideoEncoderOutput};
use core_foundation::{Array, Boolean, CFType, Dictionary, MutableDictionary, Number, OSStatus, StringRef};
use core_media::{Time, VideoCodecType};
use core_video::{PixelBuffer, PixelBufferPlane};
use std::pin::Pin;

pub struct Error {
    context: &'static str,
    inner: OSStatus,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}: {}", self.context, self.inner)
    }
}

impl std::fmt::Debug for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(self, f)
    }
}

impl std::error::Error for Error {}

trait ResultExt<T> {
    fn context(self, msg: &'static str) -> Result<T, Error>;
}

impl<T> ResultExt<T> for Result<T, OSStatus> {
    fn context(self, context: &'static str) -> Result<T, Error> {
        self.map_err(|inner| Error { context, inner })
    }
}

#[derive(Copy, Clone, Debug)]
pub enum H265ProfileLevel {
    Main3_0,
    Main3_1,
    Main4_0,
    Main4_1,
    Main5_0,
    Main5_1,
    Main5_2,
    Main6_0,
    Main6_1,
    Main6_2,
}

impl H265ProfileLevel {
    /// The computed maximum VCL bitrate, in bits per second.
    pub fn max_vcl_bitrate(self) -> u32 {
        // See H.264 table A.10. This is the value for the main profile
        // (only one currently supported).
        let cpb_vcl_factor = 1_000;

        let hbr_factor = 1; // see H.265 section A.4.2; likewise correct for main profile.
        let br_hbr_factor = cpb_vcl_factor * hbr_factor;

        self.max_br() * br_hbr_factor
    }

    /// The `MaxBR` parameter from H.265 table A.9.
    ///
    /// This is not directly meaningful; it must be multiplied by the `BrVclFactor` or `BrNalFactor`.
    pub fn max_br(self) -> u32 {
        match self {
            H265ProfileLevel::Main3_0 => 6_000,
            H265ProfileLevel::Main3_1 => 10_000,
            H265ProfileLevel::Main4_0 => 12_000,
            H265ProfileLevel::Main4_1 => 20_000,
            H265ProfileLevel::Main5_0 => 25_000,
            H265ProfileLevel::Main5_1 => 40_000,
            H265ProfileLevel::Main5_2 => 60_000,
            H265ProfileLevel::Main6_0 => 60_000,
            H265ProfileLevel::Main6_1 => 120_000,
            H265ProfileLevel::Main6_2 => 240_000,
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            H265ProfileLevel::Main3_0 => "HEVC_Main_3_0",
            H265ProfileLevel::Main3_1 => "HEVC_Main_3_1",
            H265ProfileLevel::Main4_0 => "HEVC_Main_4_0",
            H265ProfileLevel::Main4_1 => "HEVC_Main_4_1",
            H265ProfileLevel::Main5_0 => "HEVC_Main_5_0",
            H265ProfileLevel::Main5_1 => "HEVC_Main_5_1",
            H265ProfileLevel::Main5_2 => "HEVC_Main_5_2",
            H265ProfileLevel::Main6_0 => "HEVC_Main_6_0",
            H265ProfileLevel::Main6_1 => "HEVC_Main_6_1",
            H265ProfileLevel::Main6_2 => "HEVC_Main_6_2",
        }
    }
}

#[derive(Clone, Debug)]
pub enum VideoEncoderCodec {
    H264 {
        avg_bitrate: Option<u32>,
    },
    H265 {
        avg_bitrate: Option<u32>,
        level: Option<H265ProfileLevel>,
    },
}

impl From<&VideoEncoderCodec> for VideoCodecType {
    fn from(c: &VideoEncoderCodec) -> Self {
        match c {
            VideoEncoderCodec::H264 { .. } => VideoCodecType::H264,
            VideoEncoderCodec::H265 { .. } => VideoCodecType::Hevc,
        }
    }
}

#[derive(Clone, Debug)]
pub struct VideoEncoderConfig {
    pub width: u16,
    pub height: u16,
    pub fps: f64,
    pub codec: VideoEncoderCodec,
    pub input_format: VideoEncoderInputFormat,
    pub max_key_frame_interval: Option<i32>,
    pub max_bitrate: Option<u32>,
}

#[derive(Clone, Debug)]
pub enum VideoEncoderInputFormat {
    Yuv420Planar,
    Yuv444Planar,
    Bgra,
}

/// `VideoEncoder` is a wrapper around `CompressionSession` that implements `av_traits::VideoEncoder`.
pub struct VideoEncoder<F: Send> {
    sess: CompressionSession<Pin<Box<F>>>,
    config: VideoEncoderConfig,
    frame_count: u64,
}

impl<F: Send> VideoEncoder<F> {
    pub fn new(config: VideoEncoderConfig) -> Result<Self, Error> {
        let mut encoder_specification = MutableDictionary::new_cf_type();
        unsafe {
            encoder_specification.set_value(
                sys::kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder as _,
                Boolean::from(true).cf_type_ref() as _,
            );
        }

        let mut sess = CompressionSession::new(CompressionSessionConfig {
            width: config.width as _,
            height: config.height as _,
            codec_type: (&config.codec).into(),
            encoder_specification: Some(encoder_specification.into()),
        })
        .context("unable to create compression session")?;

        unsafe {
            sess.set_property(sys::kVTCompressionPropertyKey_AllowFrameReordering, Boolean::from(false))
                .context("unable to disable frame reordering")?;
            sess.set_property(sys::kVTCompressionPropertyKey_RealTime, Boolean::from(true))
                .context("unable to set realtime")?;
            sess.set_property(sys::kVTCompressionPropertyKey_ExpectedFrameRate, Number::from(config.fps))
                .context("unable to set expected frame rate")?;
            if let Some(max_key_frame_interval) = config.max_key_frame_interval {
                sess.set_property(sys::kVTCompressionPropertyKey_MaxKeyFrameInterval, Number::from(max_key_frame_interval))
                    .context("unable to set max key frame interval")?;
            }
            if let Some(max_bitrate) = config.max_bitrate {
                let data_limits = Array::from(
                    &[
                        Number::from((max_bitrate / 8) as i64), // bytes
                        Number::from(1),                        // per 1 second
                    ][..],
                );
                sess.set_property(sys::kVTCompressionPropertyKey_DataRateLimits, data_limits)
                    .context("unable to set data rate limits")?;
            }

            match &config.codec {
                VideoEncoderCodec::H264 { avg_bitrate } => {
                    if let Some(avg_bitrate) = avg_bitrate {
                        sess.set_property(sys::kVTCompressionPropertyKey_AverageBitRate, Number::from(*avg_bitrate as i64))
                            .context("unable to set average bitrate")?;
                    }
                }
                VideoEncoderCodec::H265 { avg_bitrate, level } => {
                    if let Some(avg_bitrate) = avg_bitrate {
                        sess.set_property(sys::kVTCompressionPropertyKey_AverageBitRate, Number::from(*avg_bitrate as i64))
                            .context("unable to set average bitrate")?;
                    }
                    if let Some(level) = level {
                        sess.set_property(sys::kVTCompressionPropertyKey_ProfileLevel, StringRef::from_static(level.as_str()))
                            .context("unable to set HEVC level")?;
                    }
                }
            }
        }

        sess.prepare_to_encode_frames().context("unable to prepare to encode frames")?;

        Ok(Self { sess, config, frame_count: 0 })
    }

    fn next_video_encoder_trait_frame(&mut self) -> Result<Option<VideoEncoderOutput<F>>, OSStatus> {
        Ok(match self.sess.frames().try_recv().ok().transpose()? {
            Some(frame) => {
                let raw_frame = unsafe { *Pin::into_inner_unchecked(frame.context) };
                let Some(sample_buffer) = frame.sample_buffer else {
                    return Ok(Some(VideoEncoderOutput {
                        raw_frame,
                        encoded_frame: None,
                    }));
                };

                let mut is_keyframe = true;

                unsafe {
                    if let Some(attachments) = sample_buffer.attachments_array() {
                        if !attachments.is_empty() {
                            if let Some(dict) = attachments.cf_type_value_at_index::<Dictionary>(0) {
                                if let Some(not_sync) = dict.cf_type_value::<Boolean>(sys::kCMSampleAttachmentKey_NotSync as _) {
                                    is_keyframe = !not_sync.value();
                                }
                            }
                        }
                    }
                }

                let data_buffer = sample_buffer.data_buffer().expect("all frames should have data");
                let data_buffer = data_buffer.create_contiguous(0, 0)?;
                let mut avcc_data = data_buffer.data(0)?;

                let format_desc = sample_buffer.format_description().expect("all frames should have format descriptions");
                let prefix_len = match self.config.codec {
                    VideoEncoderCodec::H264 { .. } => format_desc.h264_parameter_set_at_index(0)?.nal_unit_header_length,
                    VideoEncoderCodec::H265 { .. } => format_desc.hevc_parameter_set_at_index(0)?.nal_unit_header_length,
                };

                let mut data = Vec::with_capacity(avcc_data.len() + 100);

                // if this is a keyframe, prepend parameter sets
                if is_keyframe {
                    match self.config.codec {
                        VideoEncoderCodec::H264 { .. } => {
                            for i in 0..2 {
                                let ps = format_desc.h264_parameter_set_at_index(i)?;
                                data.extend_from_slice(&[0, 0, 0, 1]);
                                data.extend_from_slice(ps.data);
                            }
                        }
                        VideoEncoderCodec::H265 { .. } => {
                            for i in 0..3 {
                                let ps = format_desc.hevc_parameter_set_at_index(i)?;
                                data.extend_from_slice(&[0, 0, 0, 1]);
                                data.extend_from_slice(ps.data);
                            }
                        }
                    };
                }

                // convert from avcc to annex-b as we copy the data
                while avcc_data.len() > prefix_len {
                    let nalu_len = match prefix_len {
                        1 => avcc_data[0] as usize,
                        2 => (avcc_data[0] as usize) << 8 | avcc_data[1] as usize,
                        3 => (avcc_data[0] as usize) << 16 | (avcc_data[1] as usize) << 8 | avcc_data[2] as usize,
                        4 => (avcc_data[0] as usize) << 24 | (avcc_data[1] as usize) << 16 | (avcc_data[2] as usize) << 8 | avcc_data[3] as usize,
                        l => panic!("invalid nalu length: {}", l),
                    };
                    let nalu = &avcc_data[prefix_len..prefix_len + nalu_len];
                    data.extend_from_slice(&[0, 0, 0, 1]);
                    data.extend_from_slice(nalu);
                    avcc_data = &avcc_data[prefix_len + nalu_len..];
                }

                let encoded_frame = Some(EncodedVideoFrame { data, is_keyframe });

                Some(VideoEncoderOutput { encoded_frame, raw_frame })
            }
            None => None,
        })
    }
}

impl<F: RawVideoFrame<u8> + Send + Unpin> av_traits::VideoEncoder for VideoEncoder<F> {
    type Error = OSStatus;
    type RawVideoFrame = F;

    fn encode(&mut self, input: Self::RawVideoFrame, frame_type: EncodedFrameType) -> Result<Option<VideoEncoderOutput<Self::RawVideoFrame>>, Self::Error> {
        let input = Box::pin(input);

        let pixel_buffer = match self.config.input_format {
            VideoEncoderInputFormat::Yuv420Planar => unsafe {
                PixelBuffer::with_planar_bytes(
                    self.config.width as _,
                    self.config.height as _,
                    sys::kCVPixelFormatType_420YpCbCr8Planar,
                    vec![
                        PixelBufferPlane {
                            width: self.config.width as _,
                            height: self.config.height as _,
                            bytes_per_row: self.config.width as _,
                            data: input.samples(0).as_ptr() as _,
                        },
                        PixelBufferPlane {
                            width: (self.config.width / 2) as _,
                            height: (self.config.height / 2) as _,
                            bytes_per_row: (self.config.width / 2) as _,
                            data: input.samples(1).as_ptr() as _,
                        },
                        PixelBufferPlane {
                            width: (self.config.width / 2) as _,
                            height: (self.config.height / 2) as _,
                            bytes_per_row: (self.config.width / 2) as _,
                            data: input.samples(2).as_ptr() as _,
                        },
                    ],
                )?
            },
            VideoEncoderInputFormat::Yuv444Planar => unsafe {
                PixelBuffer::with_planar_bytes(
                    self.config.width as _,
                    self.config.height as _,
                    sys::kCVPixelFormatType_444YpCbCr8,
                    vec![
                        PixelBufferPlane {
                            width: self.config.width as _,
                            height: self.config.height as _,
                            bytes_per_row: self.config.width as _,
                            data: input.samples(0).as_ptr() as _,
                        },
                        PixelBufferPlane {
                            width: self.config.width as _,
                            height: self.config.height as _,
                            bytes_per_row: self.config.width as _,
                            data: input.samples(1).as_ptr() as _,
                        },
                        PixelBufferPlane {
                            width: self.config.width as _,
                            height: self.config.height as _,
                            bytes_per_row: self.config.width as _,
                            data: input.samples(2).as_ptr() as _,
                        },
                    ],
                )?
            },
            VideoEncoderInputFormat::Bgra => unsafe {
                PixelBuffer::with_bytes(
                    self.config.width as _,
                    self.config.height as _,
                    sys::kCVPixelFormatType_32BGRA,
                    4,
                    input.samples(0).as_ptr() as _,
                )?
            },
        };

        let fps_den = if self.config.fps.fract() == 0.0 { 1000 } else { 1001 };
        let fps_num = (self.config.fps * fps_den as f64).round() as i64;

        let frame_number = self.frame_count;
        self.frame_count += 1;
        self.sess
            .encode_frame(pixel_buffer.into(), Time::new((fps_den * frame_number) as _, fps_num as _), input, frame_type)?;
        self.next_video_encoder_trait_frame()
    }

    fn flush(&mut self) -> Result<Option<VideoEncoderOutput<Self::RawVideoFrame>>, Self::Error> {
        self.sess.flush()?;
        self.next_video_encoder_trait_frame()
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use av_traits::VideoEncoder as _;

    struct TestFrame {
        samples: Vec<Vec<u8>>,
    }

    impl RawVideoFrame<u8> for TestFrame {
        fn samples(&self, plane: usize) -> &[u8] {
            &self.samples[plane]
        }
    }

    fn test_video_encoder(codec: VideoEncoderCodec) {
        let mut encoder = VideoEncoder::new(VideoEncoderConfig {
            width: 1920,
            height: 1080,
            fps: 29.97,
            codec,
            input_format: VideoEncoderInputFormat::Yuv420Planar,
            max_key_frame_interval: None,
            max_bitrate: None,
        })
        .unwrap();

        let mut encoded = vec![];
        let mut encoded_frames = 0;

        let u = vec![200u8; 1920 * 1080 / 4];
        let v = vec![128u8; 1920 * 1080 / 4];
        for i in 0..90 {
            let mut y = Vec::with_capacity(1920 * 1080);
            for line in 0..1080 {
                let sample = if line / 12 == i {
                    // add some motion by drawing a line that moves from top to bottom
                    16
                } else {
                    (16.0 + (line as f64 / 1080.0) * 219.0).round() as u8
                };
                y.resize(y.len() + 1920, sample);
            }
            let frame = TestFrame {
                samples: vec![y, u.clone(), v.clone()],
            };
            if let Some(output) = encoder.encode(frame, EncodedFrameType::Auto).unwrap() {
                encoded.append(&mut output.encoded_frame.expect("frame was not dropped").data);
                encoded_frames += 1;
            }
        }
        while let Some(output) = encoder.flush().unwrap() {
            encoded.append(&mut output.encoded_frame.expect("frame was not dropped").data);
            encoded_frames += 1;
        }

        assert_eq!(encoded_frames, 90);
        assert!(encoded.len() > 5000);

        // To inspect the output, uncomment these lines:
        //use std::io::Write;
        //std::fs::File::create("tmp.bin").unwrap().write_all(&encoded).unwrap();
    }

    #[test]
    fn test_video_encoder_with_encode_frame_type() {
        let codec = VideoEncoderCodec::H264 { avg_bitrate: Some(10000) };
        let mut encoder = VideoEncoder::new(VideoEncoderConfig {
            width: 1920,
            height: 1080,
            fps: 30.0,
            codec,
            input_format: VideoEncoderInputFormat::Yuv420Planar,
            max_key_frame_interval: None,
            max_bitrate: None,
        })
        .unwrap();

        let u = vec![200u8; 1920 * 1080 / 4];
        let v = vec![128u8; 1920 * 1080 / 4];
        let mut keyframe_count = 0;
        for i in 0..360 {
            let mut y = Vec::with_capacity(1920 * 1080);
            for line in 0..1080 {
                let sample = if line / 12 == i {
                    // add some motion by drawing a line that moves from top to bottom
                    16
                } else {
                    (16.0 + (line as f64 / 1080.0) * 219.0).round() as u8
                };
                y.resize(y.len() + 1920, sample);
            }
            let frame = TestFrame {
                samples: vec![y, u.clone(), v.clone()],
            };
            if let Some(output) = encoder
                .encode(frame, if i == 0 { EncodedFrameType::Key } else { EncodedFrameType::Auto })
                .unwrap()
            {
                keyframe_count += if output.encoded_frame.expect("frame was not dropped").is_keyframe {
                    1
                } else {
                    0
                };
            }
        }
        assert!(keyframe_count > 1);
    }

    #[test]
    fn test_video_encoder_with_encode_frame_type_and_max_key_frame_interval() {
        let mut encoder = VideoEncoder::new(VideoEncoderConfig {
            width: 1920,
            height: 1080,
            fps: 30.0,
            codec: VideoEncoderCodec::H264 { avg_bitrate: Some(10000) },
            input_format: VideoEncoderInputFormat::Yuv420Planar,
            max_key_frame_interval: Some(i32::MAX),
            max_bitrate: None,
        })
        .unwrap();

        let u = vec![200u8; 1920 * 1080 / 4];
        let v = vec![128u8; 1920 * 1080 / 4];
        let mut keyframe_count = 0;
        for i in 0..360 {
            let mut y = Vec::with_capacity(1920 * 1080);
            for line in 0..1080 {
                let sample = if line / 12 == i {
                    // add some motion by drawing a line that moves from top to bottom
                    16
                } else {
                    (16.0 + (line as f64 / 1080.0) * 219.0).round() as u8
                };
                y.resize(y.len() + 1920, sample);
            }
            let frame = TestFrame {
                samples: vec![y, u.clone(), v.clone()],
            };

            if let Some(output) = encoder
                // force a keyframe every 3 seconds
                .encode(frame, if i % 90 == 0 { EncodedFrameType::Key } else { EncodedFrameType::Auto })
                .unwrap()
            {
                keyframe_count += if output.encoded_frame.expect("frame was not dropped").is_keyframe {
                    1
                } else {
                    0
                };
            }
        }
        assert_eq!(keyframe_count, 4);
    }

    #[test]
    fn test_video_encoder_h264() {
        test_video_encoder(VideoEncoderCodec::H264 { avg_bitrate: Some(10_000) });
    }

    #[test]
    fn test_video_encoder_h265() {
        test_video_encoder(VideoEncoderCodec::H265 {
            avg_bitrate: Some(10_000),
            level: None,
        });
    }
}
