use super::*;
use av_traits::{EncodedVideoFrame, RawVideoFrame, VideoEncoderOutput};
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
}

#[derive(Clone, Debug)]
pub enum VideoEncoderInputFormat {
    Yuv420Planar,
    Yuv444Planar,
}

/// `VideoEncoder` is a wrapper around `CompressionSession` that implements `av_traits::VideoEncoder`.
pub struct VideoEncoder<F: Send> {
    sess: CompressionSession<Pin<Box<F>>>,
    config: VideoEncoderConfig,
    frame_count: u64,
}

impl<F: Send> VideoEncoder<F> {
    pub fn new(config: VideoEncoderConfig) -> Result<Self, OSStatus> {
        let mut encoder_specification = MutableDictionary::default();
        unsafe {
            encoder_specification.set_value(
                sys::kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder as _,
                Boolean::from(true).cf_type_ref() as _,
            );
            encoder_specification.set_value(
                sys::kVTCompressionPropertyKey_AllowFrameReordering as _,
                Boolean::from(false).cf_type_ref() as _,
            );
            encoder_specification.set_value(sys::kVTCompressionPropertyKey_RealTime as _, Boolean::from(true).cf_type_ref() as _);
            encoder_specification.set_value(
                sys::kVTCompressionPropertyKey_ExpectedFrameRate as _,
                Number::from(config.fps).cf_type_ref() as _,
            );
        }

        match &config.codec {
            VideoEncoderCodec::H264 { bitrate } => {
                if let Some(bitrate) = bitrate {
                    unsafe {
                        encoder_specification.set_value(
                            sys::kVTCompressionPropertyKey_AverageBitRate as _,
                            Number::from(*bitrate as i64).cf_type_ref() as _,
                        );
                    }
                }
            }
            VideoEncoderCodec::H265 { bitrate } => {
                if let Some(bitrate) = bitrate {
                    unsafe {
                        encoder_specification.set_value(
                            sys::kVTCompressionPropertyKey_AverageBitRate as _,
                            Number::from(*bitrate as i64).cf_type_ref() as _,
                        );
                    }
                }
            }
        }

        Ok(Self {
            sess: CompressionSession::new(CompressionSessionConfig {
                width: config.width as _,
                height: config.height as _,
                codec_type: (&config.codec).into(),
                encoder_specification: Some(encoder_specification.into()),
            })?,
            config,
            frame_count: 0,
        })
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

    fn encode(&mut self, input: Self::RawVideoFrame) -> Result<Option<VideoEncoderOutput<Self::RawVideoFrame>>, Self::Error> {
        let input = Box::pin(input);
        let pixel_buffer = unsafe {
            PixelBuffer::with_planar_bytes(
                self.config.width as _,
                self.config.height as _,
                match self.config.input_format {
                    VideoEncoderInputFormat::Yuv420Planar => sys::kCVPixelFormatType_420YpCbCr8Planar,
                    VideoEncoderInputFormat::Yuv444Planar => sys::kCVPixelFormatType_444YpCbCr8,
                },
                match self.config.input_format {
                    VideoEncoderInputFormat::Yuv420Planar => vec![
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
                    VideoEncoderInputFormat::Yuv444Planar => vec![
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
                },
            )?
        };

        let fps_den = if self.config.fps.fract() == 0.0 { 1000 } else { 1001 };
        let fps_num = (self.config.fps * fps_den as f64).round() as i64;

        let frame_number = self.frame_count;
        self.frame_count += 1;
        self.sess
            .encode_frame(pixel_buffer.into(), Time::new((fps_den * frame_number) as _, fps_num as _), input)?;
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
            if let Some(mut output) = encoder.encode(frame).unwrap() {
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
    fn test_video_encoder_h264() {
        test_video_encoder(VideoEncoderCodec::H264 { bitrate: Some(10000) });
    }

    #[test]
    fn test_video_encoder_h265() {
        test_video_encoder(VideoEncoderCodec::H265 { bitrate: Some(10000) });
    }
}
