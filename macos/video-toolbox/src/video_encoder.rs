use super::*;
use av_traits::{EncodedFrameType, EncodedVideoFrame, RawVideoFrame, VideoEncoderOutput};
use core_foundation::{Boolean, CFType, Dictionary, MutableDictionary, Number, OSStatus};
use core_media::{Time, VideoCodecType};
use core_video::{PixelBuffer, PixelBufferPlane};
use std::pin::Pin;

#[derive(Clone, Debug)]
pub enum VideoEncoderCodec {
    H264 { bitrate: Option<u32> },
    H265 { bitrate: Option<u32> },
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
    pub fn new(config: VideoEncoderConfig) -> Result<Self, OSStatus> {
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
        })?;

        unsafe {
            sess.set_property(sys::kVTCompressionPropertyKey_AllowFrameReordering, Boolean::from(false))?;
            sess.set_property(sys::kVTCompressionPropertyKey_RealTime, Boolean::from(true))?;
            sess.set_property(sys::kVTCompressionPropertyKey_ExpectedFrameRate, Number::from(config.fps))?;
            if let Some(max_key_frame_interval) = config.max_key_frame_interval {
                sess.set_property(sys::kVTCompressionPropertyKey_MaxKeyFrameInterval, Number::from(max_key_frame_interval))?;
            }

            match &config.codec {
                VideoEncoderCodec::H264 { bitrate } => {
                    if let Some(bitrate) = bitrate {
                        sess.set_property(sys::kVTCompressionPropertyKey_AverageBitRate, Number::from(*bitrate as i64))?;
                    }
                }
                VideoEncoderCodec::H265 { bitrate } => {
                    if let Some(bitrate) = bitrate {
                        sess.set_property(sys::kVTCompressionPropertyKey_AverageBitRate, Number::from(*bitrate as i64))?;
                    }
                }
            }
        }

        sess.prepare_to_encode_frames()?;

        Ok(Self { sess, config, frame_count: 0 })
    }

    fn next_video_encoder_trait_frame(&mut self) -> Result<Option<VideoEncoderOutput<F>>, OSStatus> {
        Ok(match self.sess.frames().try_recv().ok().transpose()? {
            Some(frame) => {
                let mut is_keyframe = true;

                unsafe {
                    if let Some(attachments) = frame.sample_buffer.attachments_array() {
                        if !attachments.is_empty() {
                            if let Some(dict) = attachments.cf_type_value_at_index::<Dictionary>(0) {
                                if let Some(not_sync) = dict.cf_type_value::<Boolean>(sys::kCMSampleAttachmentKey_NotSync as _) {
                                    is_keyframe = !not_sync.value();
                                }
                            }
                        }
                    }
                }

                let data_buffer = frame.sample_buffer.data_buffer().expect("all frames should have data");
                let data_buffer = data_buffer.create_contiguous(0, 0)?;
                let mut avcc_data = data_buffer.data(0)?;

                let format_desc = frame.sample_buffer.format_description().expect("all frames should have format descriptions");
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

                let encoded_frame = EncodedVideoFrame { data, is_keyframe };

                let raw_frame = unsafe { *Pin::into_inner_unchecked(frame.context) };
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
            if let Some(mut output) = encoder.encode(frame, EncodedFrameType::Auto).unwrap() {
                encoded.append(&mut output.encoded_frame.data);
                encoded_frames += 1;
            }
        }
        while let Some(mut output) = encoder.flush().unwrap() {
            encoded.append(&mut output.encoded_frame.data);
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
        let codec = VideoEncoderCodec::H264 { bitrate: Some(10000) };
        let mut encoder = VideoEncoder::new(VideoEncoderConfig {
            width: 1920,
            height: 1080,
            fps: 30.0,
            codec,
            input_format: VideoEncoderInputFormat::Yuv420Planar,
            max_key_frame_interval: None,
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
                keyframe_count += if output.encoded_frame.is_keyframe { 1 } else { 0 };
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
            codec: VideoEncoderCodec::H264 { bitrate: Some(10000) },
            input_format: VideoEncoderInputFormat::Yuv420Planar,
            max_key_frame_interval: Some(i32::MAX),
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
                keyframe_count += if output.encoded_frame.is_keyframe { 1 } else { 0 };
            }
        }
        assert_eq!(keyframe_count, 4);
    }

    #[test]
    fn test_video_encoder_h264() {
        test_video_encoder(VideoEncoderCodec::H264 { bitrate: Some(10000) });
    }

    #[test]
    fn test_video_encoder_h265() {
        test_video_encoder(VideoEncoderCodec::H265 { bitrate: Some(10000) });
    }
}
