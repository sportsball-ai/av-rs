use av_traits::{EncodedFrameType, RawVideoFrame, VideoEncoder, VideoEncoderOutput};
use snafu::Snafu;

#[cfg(feature = "v2-compat")]
mod xcoder_259;
mod xcoder_310;

#[derive(Debug, Snafu)]
pub enum XcoderEncoderError {
    #[snafu(display("unable to allocate device session context"))]
    UnableToAllocateDeviceSessionContext,
    #[snafu(display("error {operation} (code = {code})"))]
    Unknown { code: i32, operation: &'static str },
}

type Result<T> = core::result::Result<T, XcoderEncoderError>;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum XcoderEncoderCodec {
    H264,
    H265,
}

#[derive(Clone, Debug)]
pub struct XcoderEncoderConfig {
    pub height: u16,
    pub width: u16,
    pub fps: f64,
    pub bitrate: Option<u32>,
    pub codec: XcoderEncoderCodec,
}

// Encodes video using NETINT hardware. Only YUV 420 inputs are supported.
pub enum XcoderEncoder<F> {
    #[cfg(feature = "v2-compat")]
    Xcoder259(xcoder_259::XcoderEncoder<F>),
    Xcoder310(xcoder_310::XcoderEncoder<F>),
}

impl<F> XcoderEncoder<F> {
    pub fn new(config: XcoderEncoderConfig) -> Result<Self> {
        #[cfg(not(feature = "v2-compat"))]
        return Ok(Self::Xcoder310(xcoder_310::XcoderEncoder::new(config)?));

        #[cfg(feature = "v2-compat")]
        return match xcoder_310::XcoderEncoder::new(config.clone()) {
            Ok(enc) => Ok(Self::Xcoder310(enc)),
            Err(e) => match xcoder_259::XcoderEncoder::new(config) {
                Ok(enc) => Ok(Self::Xcoder259(enc)),
                _ => Err(e),
            },
        };
    }
}

impl<F: RawVideoFrame<u8>> VideoEncoder for XcoderEncoder<F> {
    type Error = XcoderEncoderError;
    type RawVideoFrame = F;

    fn encode(&mut self, input: F, frame_type: EncodedFrameType) -> Result<Option<VideoEncoderOutput<F>>> {
        match self {
            #[cfg(feature = "v2-compat")]
            Self::Xcoder259(e) => e.encode(input, frame_type),
            Self::Xcoder310(e) => e.encode(input, frame_type),
        }
    }

    fn flush(&mut self) -> Result<Option<VideoEncoderOutput<F>>> {
        match self {
            #[cfg(feature = "v2-compat")]
            Self::Xcoder259(e) => e.flush(),
            Self::Xcoder310(e) => e.flush(),
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use av_traits::{VideoTimecode, VideoTimecodeMode};
    use h264::{Decode, U1, U32};
    use std::{
        process::{self, Command},
        sync::Arc,
    };

    #[derive(Clone, Default)]
    struct TestFrame {
        samples: Arc<Vec<Vec<u8>>>,
        timecode: Option<VideoTimecode>,
    }

    impl RawVideoFrame<u8> for TestFrame {
        fn samples(&self, plane: usize) -> &[u8] {
            &self.samples[plane]
        }

        fn timecode(&self) -> Option<&VideoTimecode> {
            self.timecode.as_ref()
        }
    }

    #[test]
    fn test_video_encoder() {
        let mut encoder = XcoderEncoder::new(XcoderEncoderConfig {
            width: 1920,
            height: 1080,
            fps: 29.97,
            bitrate: None,
            codec: XcoderEncoderCodec::H264,
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
                samples: Arc::new(vec![y, u.clone(), v.clone()]),
                ..Default::default()
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
        //std::fs::File::create("tmp.h264").unwrap().write_all(&encoded).unwrap();
    }

    /// This is a regression test. In the past, it would abort before completing due to a resource
    /// leak.
    #[test]
    fn test_video_encoder_endurance() {
        let mut encoder = XcoderEncoder::new(XcoderEncoderConfig {
            width: 480,
            height: 270,
            fps: 29.97,
            bitrate: None,
            codec: XcoderEncoderCodec::H264,
        })
        .unwrap();

        let mut encoded_frames = 0;

        const N: usize = 7000;

        let y = vec![16u8; 480 * 270];
        let u = vec![200u8; 480 * 270 / 4];
        let v = vec![128u8; 480 * 270 / 4];

        let frame = TestFrame {
            samples: Arc::new(vec![y, u, v]),
            ..Default::default()
        };

        for _ in 0..N {
            if encoder.encode(frame.clone(), EncodedFrameType::Auto).unwrap().is_some() {
                encoded_frames += 1;
            }
        }
        while encoder.flush().unwrap().is_some() {
            encoded_frames += 1;
        }

        assert_eq!(encoded_frames, N);
    }

    /// This is a regression test. In the past, creating an encoder would open up file descriptors
    /// for /dev/shm/NI_RETRY_LCK_ENCODERS and other files and never close them.
    #[test]
    fn test_video_encoder_file_descriptors() {
        for _ in 0..400 {
            XcoderEncoder::<TestFrame>::new(XcoderEncoderConfig {
                width: 480,
                height: 270,
                fps: 29.97,
                bitrate: None,
                codec: XcoderEncoderCodec::H264,
            })
            .unwrap();
        }

        let lsof_output = Command::new("lsof").args(["-p", &process::id().to_string()]).output().unwrap();
        let lsof_output = String::from_utf8_lossy(&lsof_output.stdout);
        println!("{}", lsof_output);
        let shm_file_descriptors = lsof_output.matches("/dev/shm/NI_RETRY_LCK_ENCODERS").count();
        let nvme_file_descriptors = lsof_output.matches("/dev/nvme").count();
        // If this is the only test running, these should be zero. But even with other tests
        // running there should be a very small number of open descriptors.
        assert!(shm_file_descriptors < 100);
        assert!(nvme_file_descriptors < 100);
    }

    #[test]
    fn test_video_encoder_timecodes() {
        let mut encoder = XcoderEncoder::new(XcoderEncoderConfig {
            width: 480,
            height: 270,
            fps: 29.97,
            bitrate: None,
            codec: XcoderEncoderCodec::H264,
        })
        .unwrap();

        let mut encoded = vec![];
        let mut encoded_frames = 0;

        let y = vec![16u8; 480 * 270];
        let u = vec![200u8; 480 * 270 / 4];
        let v = vec![128u8; 480 * 270 / 4];

        let frame = TestFrame {
            samples: Arc::new(vec![y, u, v]),
            timecode: Some(VideoTimecode {
                hours: 1,
                minutes: 2,
                seconds: 3,
                frames: 4,
                discontinuity: false,
                mode: VideoTimecodeMode::Normal,
            }),
        };

        for _ in 0..30 {
            if let Some(mut output) = encoder.encode(frame.clone(), EncodedFrameType::Auto).unwrap() {
                encoded.append(&mut output.encoded_frame.data);
                encoded_frames += 1;
            }
        }
        while let Some(mut output) = encoder.flush().unwrap() {
            encoded.append(&mut output.encoded_frame.data);
            encoded_frames += 1;
        }

        assert_eq!(encoded_frames, 30);

        let mut vui_parameters = None;
        for nalu in h264::iterate_annex_b(&encoded) {
            let nalu = h264::NALUnit::decode(h264::Bitstream::new(nalu.iter().copied())).unwrap();
            match nalu.nal_unit_type.0 {
                h264::NAL_UNIT_TYPE_SEQUENCE_PARAMETER_SET => {
                    let sps = h264::SequenceParameterSet::decode(&mut h264::Bitstream::new(nalu.rbsp_byte)).unwrap();
                    assert_eq!(
                        sps.vui_parameters,
                        h264::VUIParameters {
                            timing_info_present_flag: U1(1),
                            fixed_frame_rate_flag: U1(1),
                            num_units_in_tick: U32(1001),
                            time_scale: U32(60000),
                            pic_struct_present_flag: U1(1),
                            ..Default::default()
                        }
                    );
                    vui_parameters = Some(sps.vui_parameters);
                }
                h264::NAL_UNIT_TYPE_SUPPLEMENTAL_ENHANCEMENT_INFORMATION => {
                    let vui_parameters = vui_parameters.as_ref().unwrap();

                    let sei = h264::SEI::decode(&mut h264::Bitstream::new(nalu.rbsp_byte)).unwrap();
                    assert_eq!(sei.sei_message.len(), 1);
                    let message = sei.sei_message.into_iter().next().unwrap();

                    assert_eq!(message.payload_type, h264::SEI_PAYLOAD_TYPE_PIC_TIMING);
                    let pic_timing = h264::PicTiming::decode(&mut h264::Bitstream::new(message.payload), vui_parameters).unwrap();
                    assert_eq!(pic_timing.pic_struct.0, 0);
                    assert_eq!(pic_timing.timecodes.len(), 1);

                    let timecode = &pic_timing.timecodes[0];
                    assert_eq!(timecode.full_timestamp_flag.0, 1);
                    assert_eq!(timecode.discontinuity_flag.0, 0);
                    assert_eq!(timecode.hours.0, 1);
                    assert_eq!(timecode.minutes.0, 2);
                    assert_eq!(timecode.seconds.0, 3);
                    assert_eq!(timecode.n_frames.0, 4);
                }
                _ => {}
            }
        }
    }
}
