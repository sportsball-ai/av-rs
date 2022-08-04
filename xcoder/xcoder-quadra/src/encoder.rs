use av_traits::{EncodedFrameType, EncodedVideoFrame, RawVideoFrame, VideoEncoder, VideoEncoderOutput};
use scopeguard::{guard, ScopeGuard};
use snafu::Snafu;
use std::{collections::VecDeque, mem};
use xcoder_quadra_sys as sys;

#[derive(Debug, Snafu)]
pub enum XcoderEncoderError {
    #[snafu(display("unable to allocate device session context"))]
    UnableToAllocateDeviceSessionContext,
    #[snafu(display("error {operation} (code = {code})"))]
    Unknown { code: sys::ni_retcode_t, operation: &'static str },
}

type Result<T> = core::result::Result<T, XcoderEncoderError>;

struct EncodedFrame {
    data_io: sys::ni_session_data_io_t,
    parameter_sets: Option<Vec<u8>>,
}

impl EncodedFrame {
    pub fn new() -> Result<Self> {
        let packet = unsafe {
            let mut packet = mem::zeroed();
            let code = sys::ni_packet_buffer_alloc(&mut packet as _, sys::NI_MAX_TX_SZ as _);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderEncoderError::Unknown {
                    code,
                    operation: "allocating packet buffer",
                });
            }
            packet
        };

        Ok(Self {
            data_io: sys::ni_session_data_io_t {
                data: sys::_ni_session_data_io__bindgen_ty_1 { packet },
            },
            parameter_sets: None,
        })
    }

    pub fn as_slice(&self) -> &[u8] {
        let data = unsafe { &std::slice::from_raw_parts(self.data_io.data.packet.p_data as _, self.data_io.data.packet.data_len as _) };
        &data[sys::NI_FW_ENC_BITSTREAM_META_DATA_SIZE as usize..]
    }

    /// When parameter sets are emitted by the encoder, they're provided here.
    pub fn parameter_sets(&self) -> Option<&Vec<u8>> {
        self.parameter_sets.as_ref()
    }

    pub fn packet(&self) -> &sys::ni_packet_t {
        unsafe { &self.data_io.data.packet }
    }

    pub fn is_key_frame(&self) -> bool {
        self.packet().frame_type == 0
    }

    pub fn as_data_io_mut_ptr(&mut self) -> *mut sys::ni_session_data_io_t {
        &mut self.data_io as _
    }
}

impl Drop for EncodedFrame {
    fn drop(&mut self) {
        unsafe {
            sys::ni_packet_buffer_free(&mut self.data_io.data.packet as _);
        }
    }
}

// Encodes video using NETINT hardware. Only YUV 420 inputs are supported.
pub struct XcoderEncoder<F> {
    session: *mut sys::ni_session_context_t,
    config: XcoderEncoderConfig,
    did_start: bool,
    did_finish: bool,
    did_flush: bool,
    frames_copied: u64,
    encoded_frame: EncodedFrame,
    frame_data_io: sys::ni_session_data_io_t,
    frame_data_io_has_next_frame: bool,
    frame_data_strides: [i32; sys::NI_MAX_NUM_DATA_POINTERS as usize],
    frame_data_heights: [i32; sys::NI_MAX_NUM_DATA_POINTERS as usize],
    input_frames: VecDeque<F>,
    output_frames: VecDeque<VideoEncoderOutput<F>>,
}

#[derive(Clone, Debug)]
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

impl<F> XcoderEncoder<F> {
    pub fn new(config: XcoderEncoderConfig) -> Result<Self> {
        let fps_denominator = if config.fps.fract() == 0.0 {
            1000
        } else {
            // a denominator of 1001 for 29.97 or 59.94 is more
            // conventional
            1001
        };
        let fps_numerator = (config.fps * fps_denominator as f64).round() as _;

        unsafe {
            let mut params: sys::ni_xcoder_params_t = mem::zeroed();
            let code = sys::ni_encoder_init_default_params(
                &mut params as _,
                fps_numerator,
                fps_denominator,
                // There's nothing special about this default bitrate. It's not used unless
                // `rc.enable_rate_control == 1`, so any valid number will do.
                config.bitrate.unwrap_or(200000) as _,
                config.width as _,
                config.height as _,
                match config.codec {
                    XcoderEncoderCodec::H264 => sys::_ni_codec_format_NI_CODEC_FORMAT_H264,
                    XcoderEncoderCodec::H265 => sys::_ni_codec_format_NI_CODEC_FORMAT_H265,
                },
            );
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderEncoderError::Unknown {
                    code,
                    operation: "initializing parameters",
                });
            }

            let cfg_enc_params = &mut params.__bindgen_anon_1.cfg_enc_params;
            cfg_enc_params.planar = 1;
            cfg_enc_params.rc.enable_rate_control = if config.bitrate.is_some() { 1 } else { 0 };

            // we don't like b-frames. so we use ipppp...
            cfg_enc_params.gop_preset_index = 9;

            // some formats like mpeg-ts require access unit delimiters
            cfg_enc_params.EnableAUD = 1;

            let mut frame_data_strides = [0; sys::NI_MAX_NUM_DATA_POINTERS as usize];
            let mut frame_data_heights = [0; sys::NI_MAX_NUM_DATA_POINTERS as usize];
            sys::ni_get_hw_yuv420p_dim(
                config.width as _,
                config.height as _,
                1, // bit depth factor
                0, // is nv12
                frame_data_strides.as_mut_ptr(),
                frame_data_heights.as_mut_ptr(),
            );

            let params = Box::new(params);

            let session = sys::ni_device_session_context_alloc_init();
            if session.is_null() {
                return Err(XcoderEncoderError::UnableToAllocateDeviceSessionContext);
            }
            let mut session = guard(session, |session| {
                sys::ni_device_session_context_free(session);
            });

            (**session).hw_id = -1;
            (**session).p_session_config = &*params as *const sys::ni_xcoder_params_t as _;
            (**session).codec_format = match config.codec {
                XcoderEncoderCodec::H264 => sys::_ni_codec_format_NI_CODEC_FORMAT_H264,
                XcoderEncoderCodec::H265 => sys::_ni_codec_format_NI_CODEC_FORMAT_H265,
            };
            (**session).src_bit_depth = 8;
            (**session).src_endian = sys::NI_FRAME_LITTLE_ENDIAN as _;
            (**session).bit_depth_factor = 1;

            let code = sys::ni_device_session_open(*session, sys::ni_device_type_t_NI_DEVICE_TYPE_ENCODER);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderEncoderError::Unknown {
                    code,
                    operation: "opening device session",
                });
            }
            let did_finish = guard(false, |did_finish| {
                sys::ni_device_session_close(*session, if did_finish { 1 } else { 0 }, sys::ni_device_type_t_NI_DEVICE_TYPE_ENCODER);
                sys::ni_device_close((**session).device_handle);
                sys::ni_device_close((**session).blk_io_handle);
            });

            let frame_data_io = {
                let mut frame: sys::ni_frame_t = std::mem::zeroed();
                let code = sys::ni_encoder_frame_buffer_alloc(
                    &mut frame as _,
                    config.width as _,
                    frame_data_heights[0],
                    frame_data_strides.as_mut_ptr(),
                    if matches!(config.codec, XcoderEncoderCodec::H264) { 1 } else { 0 },
                    sys::NI_APP_ENC_FRAME_META_DATA_SIZE as _,
                    false,
                );
                if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                    return Err(XcoderEncoderError::Unknown {
                        code,
                        operation: "allocating frame buffer",
                    });
                }
                sys::ni_session_data_io_t {
                    data: sys::_ni_session_data_io__bindgen_ty_1 { frame },
                }
            };
            let frame_data_io = guard(frame_data_io, |mut frame_data_io| {
                sys::ni_frame_buffer_free(&mut frame_data_io.data.frame as _);
            });

            let encoded_frame = EncodedFrame::new()?;

            Ok(Self {
                config,
                did_start: false,
                did_finish: ScopeGuard::into_inner(did_finish),
                did_flush: false,
                session: ScopeGuard::into_inner(session),
                encoded_frame,
                frame_data_io: ScopeGuard::into_inner(frame_data_io),
                frame_data_io_has_next_frame: false,
                frame_data_strides,
                frame_data_heights,
                frames_copied: 0,
                input_frames: VecDeque::new(),
                output_frames: VecDeque::new(),
            })
        }
    }

    fn try_reading_encoded_frames(&mut self) -> Result<()> {
        unsafe {
            while !self.did_finish {
                let code = sys::ni_device_session_read(
                    self.session,
                    self.encoded_frame.as_data_io_mut_ptr(),
                    sys::ni_device_type_t_NI_DEVICE_TYPE_ENCODER,
                );
                if code < 0 {
                    return Err(XcoderEncoderError::Unknown { code, operation: "reading" });
                }
                let packet = self.encoded_frame.packet();
                self.did_finish = packet.end_of_stream != 0;
                if code > 0 {
                    if packet.pts == 0 && packet.avg_frame_qp == 0 {
                        self.encoded_frame.parameter_sets = Some(self.encoded_frame.as_slice().to_vec());
                    } else {
                        self.output_frames.push_back(VideoEncoderOutput {
                            raw_frame: self
                                .input_frames
                                .pop_front()
                                .expect("there should never be more output frames than input frames"),
                            encoded_frame: EncodedVideoFrame {
                                data: match self.encoded_frame.parameter_sets() {
                                    Some(sets) => [sets, self.encoded_frame.as_slice()].concat(),
                                    None => self.encoded_frame.as_slice().to_vec(),
                                },
                                is_keyframe: self.encoded_frame.is_key_frame(),
                            },
                        });
                        self.encoded_frame.parameter_sets = None;
                        break;
                    }
                } else {
                    break;
                }
            }
        }
        Ok(())
    }
}

impl<F: RawVideoFrame<u8>> XcoderEncoder<F> {
    /// Attempts to write the decoded frame to the encoder, performing cropping and scaling
    /// beforehand if necessary. If Some is returned, the caller must try again with the same
    /// frame later.
    fn try_write_frame(&mut self, f: F, force_key_frame: bool) -> Result<Option<F>> {
        if !self.frame_data_io_has_next_frame {
            let mut frame = unsafe { &mut self.frame_data_io.data.frame };
            frame.start_of_stream = if self.did_start { 0 } else { 1 };
            frame.extra_data_len = sys::NI_APP_ENC_FRAME_META_DATA_SIZE as _;
            frame.pts = self.frames_copied as _;
            frame.dts = frame.pts;
            if force_key_frame {
                frame.force_key_frame = 1;
                frame.ni_pict_type = sys::ni_pic_type_t_PIC_TYPE_IDR;
            } else {
                frame.force_key_frame = 0;
                frame.ni_pict_type = 0;
            }

            let mut dst_data = [frame.p_data[0] as *mut u8, frame.p_data[1] as *mut u8, frame.p_data[2] as *mut u8];

            let mut src_data = [
                f.samples(0).as_ptr() as *mut u8,
                f.samples(1).as_ptr() as *mut u8,
                f.samples(2).as_ptr() as *mut u8,
            ];
            let mut src_strides = [self.config.width as i32, self.config.width as i32 / 2, self.config.width as i32 / 2];
            let mut src_heights = [self.config.height as i32, self.config.height as i32 / 2, self.config.height as i32 / 2];

            unsafe {
                sys::ni_copy_hw_yuv420p(
                    dst_data.as_mut_ptr(),
                    src_data.as_mut_ptr(),
                    self.config.width as _,
                    self.config.height as _,
                    1,
                    0,
                    0,
                    self.frame_data_strides.as_mut_ptr(),
                    self.frame_data_heights.as_mut_ptr(),
                    src_strides.as_mut_ptr(),
                    src_heights.as_mut_ptr(),
                );
            }
            self.frame_data_io_has_next_frame = true;
            self.frames_copied += 1;
        }

        self.did_start = true;
        let written = unsafe { sys::ni_device_session_write(self.session, &mut self.frame_data_io, sys::ni_device_type_t_NI_DEVICE_TYPE_ENCODER) };
        if written < 0 {
            return Err(XcoderEncoderError::Unknown {
                code: written,
                operation: "writing",
            });
        }

        if written > 0 {
            self.frame_data_io_has_next_frame = false;
            self.input_frames.push_back(f);
            Ok(None)
        } else {
            Ok(Some(f))
        }
    }
}

impl<F> Drop for XcoderEncoder<F> {
    fn drop(&mut self) {
        unsafe {
            sys::ni_frame_buffer_free(&mut self.frame_data_io.data.frame as _);
            sys::ni_device_session_close(self.session, if self.did_finish { 1 } else { 0 }, sys::ni_device_type_t_NI_DEVICE_TYPE_ENCODER);
            sys::ni_device_close((*self.session).device_handle);
            sys::ni_device_close((*self.session).blk_io_handle);
            sys::ni_device_session_context_free(self.session);
        }
    }
}

impl<F: RawVideoFrame<u8>> VideoEncoder for XcoderEncoder<F> {
    type Error = XcoderEncoderError;
    type RawVideoFrame = F;

    fn encode(&mut self, mut input: F, frame_type: EncodedFrameType) -> Result<Option<VideoEncoderOutput<F>>> {
        loop {
            self.try_reading_encoded_frames()?;
            match self.try_write_frame(
                input,
                match frame_type {
                    EncodedFrameType::Key => true,
                    EncodedFrameType::Auto => false,
                },
            )? {
                Some(frame) => input = frame,
                None => break,
            }
        }
        Ok(self.output_frames.pop_front())
    }

    fn flush(&mut self) -> Result<Option<VideoEncoderOutput<F>>> {
        if !self.did_flush {
            let code = unsafe { sys::ni_device_session_flush(self.session, sys::ni_device_type_t_NI_DEVICE_TYPE_ENCODER) };
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderEncoderError::Unknown { code, operation: "flushing" });
            }
            self.did_flush = true;
        }
        loop {
            if let Some(f) = self.output_frames.pop_front() {
                return Ok(Some(f));
            } else if self.did_finish || self.input_frames.is_empty() {
                return Ok(None);
            }
            self.try_reading_encoded_frames()?;
        }
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
        //std::fs::File::create("tmp.h264").unwrap().write_all(&encoded).unwrap();
    }
}
