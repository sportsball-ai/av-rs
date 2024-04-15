use super::mpp;
use av_traits::{EncodedFrameType, EncodedVideoFrame, RawVideoFrame, VideoEncoder, VideoEncoderOutput};
use rkmpp_sys as sys;
use snafu::Snafu;
use std::collections::VecDeque;

#[derive(Debug, Snafu)]
pub enum RkMppEncoderError {
    #[snafu(display("unable to load the rockchip mpp library"))]
    LibraryUnavailable,
    #[snafu(context(false), display("mpp error: {source}"))]
    LibraryError { source: mpp::Error },
    #[snafu(display("got unexpected plane layout"))]
    UnexpectedPlaneLayout,
}

type Result<T> = core::result::Result<T, RkMppEncoderError>;

pub struct RkMppEncoder<F> {
    config: RkMppEncoderConfig,
    lib: mpp::Lib,
    context: mpp::Context,
    pending_frames: VecDeque<F>,
    frame_buffer: mpp::Buffer,
    parameter_sets: Vec<u8>,
    frames_emitted: u64,
}

#[derive(Clone, Copy, Debug)]
pub enum RkMppEncoderInputFormat {
    Yuv420Planar,
    Bgra,
}

impl RkMppEncoderInputFormat {
    fn to_mpp(self) -> sys::MppFrameFormat {
        match self {
            RkMppEncoderInputFormat::Yuv420Planar => sys::MppFrameFormat_MPP_FMT_YUV420P,
            RkMppEncoderInputFormat::Bgra => sys::MppFrameFormat_MPP_FMT_BGRA8888,
        }
    }

    fn planes(&self) -> usize {
        match self {
            RkMppEncoderInputFormat::Yuv420Planar => 3,
            RkMppEncoderInputFormat::Bgra => 1,
        }
    }
}

#[derive(Clone, Debug)]
pub struct RkMppEncoderConfig {
    pub height: u16,
    pub width: u16,
    pub fps: f64,
    pub bitrate: Option<u32>,
    pub keyframe_interval: Option<u32>,
    pub input_format: RkMppEncoderInputFormat,
}

impl RkMppEncoderConfig {
    fn horizontal_stride(&self) -> u32 {
        match self.input_format {
            RkMppEncoderInputFormat::Yuv420Planar => self.width as u32,
            RkMppEncoderInputFormat::Bgra => self.width as u32 * 4,
        }
    }

    fn frame_size(&self) -> usize {
        match self.input_format {
            RkMppEncoderInputFormat::Yuv420Planar => self.width as usize * self.height as usize * 3 / 2,
            RkMppEncoderInputFormat::Bgra => self.width as usize * self.height as usize * 4,
        }
    }
}

impl<F> RkMppEncoder<F> {
    pub fn new(config: RkMppEncoderConfig) -> Result<Self> {
        let lib = mpp::Lib::new().ok_or(RkMppEncoderError::LibraryUnavailable)?;

        let mut context = lib.new_context(sys::MppCodingType_MPP_VIDEO_CodingAVC)?;

        let mut mpp_config = lib.new_config()?;
        context.get_config(&mut mpp_config)?;

        mpp_config.set_s32("prep:width", config.width as i32)?;
        mpp_config.set_s32("prep:height", config.height as i32)?;
        mpp_config.set_s32("prep:hor_stride", config.horizontal_stride() as _)?;
        mpp_config.set_s32("prep:ver_stride", config.height as i32)?;
        mpp_config.set_s32("prep:format", config.input_format.to_mpp() as i32)?;

        if let Some(bitrate) = config.bitrate {
            mpp_config.set_s32("rc:mode", sys::MppEncRcMode_e_MPP_ENC_RC_MODE_CBR as _)?;
            mpp_config.set_s32("rc:bps_target", bitrate as _)?;
            mpp_config.set_s32("rc:bps_max", (bitrate * 17 / 16) as _)?;
            mpp_config.set_s32("rc:bps_min", (bitrate * 15 / 16) as _)?;
        }

        let fps_denominator = if config.fps.fract() == 0.0 {
            1000
        } else {
            // a denominator of 1001 for 29.97 or 59.94 is more
            // conventional
            1001
        };
        let fps_numerator = (config.fps * fps_denominator as f64).round() as _;
        mpp_config.set_s32("rc:fps_in_num", fps_numerator)?;
        // There was a typo in the MPP implementation that was later fixed in
        // https://github.com/rockchip-linux/mpp/commit/02a35cb871bb848a1f0538c86984a1b3e01937fd
        //
        // We try setting the FPS "denorm" first, since that should work in all implementations,
        // but in case they remove backwards compatibility, we also try the correct "denom" version.
        mpp_config
            .set_s32("rc:fps_in_denorm", fps_denominator)
            .or_else(|_| mpp_config.set_s32("rc:fps_in_denom", fps_denominator))?;
        mpp_config.set_s32("rc:fps_out_num", fps_numerator)?;
        mpp_config
            .set_s32("rc:fps_out_denorm", fps_denominator)
            .or_else(|_| mpp_config.set_s32("rc:fps_out_denom", fps_denominator))?;

        if let Some(interval) = config.keyframe_interval {
            mpp_config.set_s32("rc:gop", interval as _)?;
        }

        mpp_config.set_s32("codec:type", sys::MppCodingType_MPP_VIDEO_CodingAVC as _)?;

        context.set_config(&mpp_config)?;

        let mut buffer_group = lib.new_buffer_group()?;
        let frame_buffer = buffer_group.get_buffer(config.frame_size() as _)?;

        let parameter_sets = {
            let mut packet_buffer = buffer_group.get_buffer(10 * 1024)?;
            let mut packet = mpp::Packet::with_buffer(&mut packet_buffer)?;
            context.get_encoder_header_sync_packet(&mut packet)?;
            packet.as_slice().to_vec()
        };

        Ok(Self {
            config,
            pending_frames: VecDeque::new(),
            lib,
            context,
            frame_buffer,
            parameter_sets,
            frames_emitted: 0,
        })
    }

    fn next_output(&mut self) -> Result<Option<VideoEncoderOutput<F>>> {
        if self.pending_frames.is_empty() {
            return Ok(None);
        }

        let packet = match self.context.encode_get_packet()? {
            Some(packet) => packet,
            None => return Ok(None),
        };
        let mut data = vec![];

        let f = self.pending_frames.pop_front().expect("we already checked for frames");
        let is_keyframe = packet.meta().get_s32(sys::MppMetaKey_e_KEY_OUTPUT_INTRA)? != 0;

        if is_keyframe && self.frames_emitted > 0 {
            // The encoder will automatically add parameter sets to the first keyframe, but we want
            // to repeat them for all keyframes.
            data.extend_from_slice(&self.parameter_sets);
        }

        data.extend_from_slice(packet.as_slice());

        self.frames_emitted += 1;
        Ok(Some(VideoEncoderOutput {
            raw_frame: f,
            encoded_frame: Some(EncodedVideoFrame { data, is_keyframe }),
        }))
    }
}

impl<F: RawVideoFrame<u8>> VideoEncoder for RkMppEncoder<F> {
    type Error = RkMppEncoderError;
    type RawVideoFrame = F;

    fn encode(&mut self, input: F, frame_type: EncodedFrameType) -> Result<Option<VideoEncoderOutput<F>>> {
        let mut mpp_frame = self.lib.new_frame()?;
        unsafe {
            self.lib.sys.mpp_frame_set_width(*mpp_frame, self.config.width as _);
            self.lib.sys.mpp_frame_set_height(*mpp_frame, self.config.height as _);
            self.lib.sys.mpp_frame_set_hor_stride(*mpp_frame, self.config.horizontal_stride());
            self.lib.sys.mpp_frame_set_ver_stride(*mpp_frame, self.config.height as _);
            self.lib.sys.mpp_frame_set_fmt(*mpp_frame, self.config.input_format.to_mpp());
        }

        {
            let mut buffer = self.frame_buffer.sync();
            let dest = buffer.as_mut_slice();

            let mut offset = 0;
            for i in 0..self.config.input_format.planes() {
                let samples = input.samples(i);
                if samples.len() > dest.len() - offset {
                    return Err(RkMppEncoderError::UnexpectedPlaneLayout);
                }
                dest[offset..offset + samples.len()].copy_from_slice(samples);
                offset += samples.len();
            }
            if offset != dest.len() {
                return Err(RkMppEncoderError::UnexpectedPlaneLayout);
            }
        }

        mpp_frame.set_buffer(&self.frame_buffer);

        match frame_type {
            EncodedFrameType::Key => self.context.force_keyframe()?,
            EncodedFrameType::Auto => {}
        }

        self.context.encode_put_frame(&mpp_frame)?;
        self.pending_frames.push_back(input);

        self.next_output()
    }

    fn flush(&mut self) -> Result<Option<VideoEncoderOutput<F>>> {
        self.next_output()
    }
}

#[cfg(test)]
mod test {
    use super::*;

    struct TestFrame {
        samples: Vec<Vec<u8>>,
    }

    impl RawVideoFrame<u8> for TestFrame {
        fn samples(&self, plane: usize) -> &[u8] {
            &self.samples[plane]
        }
    }

    #[test]
    fn test_video_encoder() {
        let mut encoder = RkMppEncoder::new(RkMppEncoderConfig {
            width: 1920,
            height: 1080,
            fps: 30.0,
            bitrate: Some(50000),
            keyframe_interval: Some(120),
            input_format: RkMppEncoderInputFormat::Yuv420Planar,
        })
        .unwrap();

        let mut encoded = vec![];
        let mut encoded_frames = 0;
        let mut encoded_keyframes = 0;

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
            if let Some(output) = encoder
                .encode(frame, if i % 30 == 0 { EncodedFrameType::Key } else { EncodedFrameType::Auto })
                .unwrap()
            {
                let mut encoded_frame = output.encoded_frame.expect("frame was not dropped");
                encoded_frames += 1;
                if encoded_frame.is_keyframe {
                    encoded_keyframes += 1;
                    if encoded_frame.data.windows(5).find(|w| w == &[0, 0, 0, 1, 0x67]).is_none() {
                        panic!("keyframe {} does not contain sps", encoded_keyframes);
                    }
                }
                encoded.append(&mut encoded_frame.data);
            }
        }
        while let Some(output) = encoder.flush().unwrap() {
            let mut encoded_frame = output.encoded_frame.expect("frame was not dropped");
            encoded.append(&mut encoded_frame.data);
            encoded_frames += 1;
            if encoded_frame.is_keyframe {
                encoded_keyframes += 1;
            }
        }

        assert_eq!(encoded_frames, 90);
        assert!(encoded.len() > 5000);
        assert!(encoded.len() < 45000);

        assert_eq!(encoded_keyframes, 3);

        // To inspect the output, uncomment these lines:
        //use std::io::Write;
        //std::fs::File::create("tmp.h264").unwrap().write_all(&encoded).unwrap();
    }
}
