use super::{Result, XcoderEncoderCodec, XcoderEncoderConfig, XcoderEncoderError};
use av_traits::{EncodedFrameType, EncodedVideoFrame, RawVideoFrame, VideoEncoder, VideoEncoderOutput};
use h264::{Encode, U1};
use scopeguard::{guard, ScopeGuard};
use std::{collections::VecDeque, io, mem};
use xcoder_logan_259_sys as sys;

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
    input_frames: VecDeque<F>,
    output_frames: VecDeque<VideoEncoderOutput<F>>,
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
            let mut params: sys::ni_encoder_params_t = mem::zeroed();
            let code = sys::ni_encoder_init_default_params(
                &mut params as _,
                fps_numerator,
                fps_denominator,
                // There's nothing special about this default bitrate. It's not used unless
                // `rc.enable_rate_control == 1`, so any valid number will do.
                config.bitrate.unwrap_or(200000) as _,
                config.width as _,
                config.height as _,
            );
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderEncoderError::Unknown {
                    code,
                    operation: "initializing parameters",
                });
            }

            params.hevc_enc_params.rc.enable_rate_control = if config.bitrate.is_some() { 1 } else { 0 };

            // we don't like b-frames. so we use ipppp...
            params.hevc_enc_params.gop_preset_index = 6;

            // some formats like mpeg-ts require access unit delimiters
            params.enable_aud = 1;

            // H.264 requires 16-byte alignment while H.265 requires 8.
            let align_to_16_bytes = config.codec == XcoderEncoderCodec::H264;
            let aligned_height = if align_to_16_bytes {
                ((config.height + 15) / 16) * 16
            } else {
                ((config.height + 7) / 8) * 8
            };
            if aligned_height > config.height {
                // If we added alignment pixels, we need the codec to crop out the bottom.
                params.hevc_enc_params.conf_win_bottom = (aligned_height - config.height) as _;
            }
            let aligned_width = if align_to_16_bytes {
                ((config.width + 15) / 16) * 16
            } else {
                ((config.width + 7) / 8) * 8
            };
            if aligned_width > config.width {
                // If we added alignment pixels, we need the codec to crop out the right.
                params.hevc_enc_params.conf_win_right = (aligned_width - config.width) as _;
            }

            if config.codec == XcoderEncoderCodec::H264 {
                // We have to override the default VUI to indicate that pic struct is present in
                // any SEI messages we may add.
                let vui = h264::VUIParameters {
                    timing_info_present_flag: U1(1),
                    fixed_frame_rate_flag: U1(1),
                    pic_struct_present_flag: U1(1),
                    ..Default::default()
                };
                let mut dest = io::Cursor::new(params.ui8VuiRbsp.as_mut_slice());
                let vui_bits = {
                    let mut bs = h264::BitstreamWriter::new(&mut dest);
                    vui.encode(&mut bs).expect("the vui should fit within the max length");
                    bs.inner().position() as usize * 8 + bs.unwritten_bits()
                };
                params.ui32VuiDataSizeBytes = (vui_bits as u32 + 7) / 8;
                params.ui32VuiDataSizeBits = vui_bits as _;
                // libxcoder needs to know where to fill in the timing info
                params.pos_num_units_in_tick = 5;
                params.pos_time_scale = params.pos_num_units_in_tick + 32;
            }

            let params = Box::new(params);

            let session = sys::ni_device_session_context_alloc_init();
            if session.is_null() {
                return Err(XcoderEncoderError::UnableToAllocateDeviceSessionContext);
            }
            let mut session = guard(session, |session| {
                sys::ni_device_session_context_free(session);
            });

            (**session).hw_id = -1;
            (**session).p_session_config = &*params as *const sys::ni_encoder_params_t as _;
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
                let code = sys::ni_frame_buffer_alloc(
                    &mut frame as _,
                    config.width as _,
                    config.height as _,
                    if align_to_16_bytes { 1 } else { 0 },
                    1,
                    if (**session).src_bit_depth > 8 { 2 } else { 1 },
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
                frames_copied: 0,
                input_frames: VecDeque::new(),
                output_frames: VecDeque::new(),
            })
        }
    }
}

impl<F: RawVideoFrame<u8>> XcoderEncoder<F> {
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
                    if (*self.session).pkt_num == 0 {
                        // If we don't set pkt_num, libxcoder doesn't free up packet buffer
                        // resources and will assert after 6000 frames. This mimics the FFmpeg
                        // patch by setting it to 1 after the first successful read.
                        (*self.session).pkt_num = 1;
                    }
                    if packet.pts == 0 && packet.avg_frame_qp == 0 {
                        self.encoded_frame.parameter_sets = Some(self.encoded_frame.as_slice().to_vec());
                    } else {
                        let raw_frame = self
                            .input_frames
                            .pop_front()
                            .expect("there should never be more output frames than input frames");
                        let data = match self.encoded_frame.parameter_sets() {
                            Some(sets) => self.config.assemble_access_unit(
                                h264::iterate_annex_b(sets).chain(h264::iterate_annex_b(&self.encoded_frame.as_slice())),
                                raw_frame.timecode(),
                            ),
                            None => self
                                .config
                                .assemble_access_unit(h264::iterate_annex_b(&self.encoded_frame.as_slice()), raw_frame.timecode()),
                        };
                        self.output_frames.push_back(VideoEncoderOutput {
                            raw_frame,
                            encoded_frame: Some(EncodedVideoFrame {
                                data,
                                is_keyframe: self.encoded_frame.is_key_frame(),
                            }),
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

    /// Attempts to write the decoded frame to the encoder, performing cropping and scaling
    /// beforehand if necessary. If Some is returned, the caller must try again with the same
    /// frame later.
    fn try_write_frame(&mut self, f: F, force_key_frame: bool) -> Result<Option<F>> {
        if !self.frame_data_io_has_next_frame {
            let frame = unsafe { &mut self.frame_data_io.data.frame };
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

            for i in 0..3 {
                let subsampling = match i {
                    0 => 1,
                    _ => 2,
                };
                let height = self.config.height as usize / subsampling;
                let src_line_stride = self.config.width as usize / subsampling;
                let dest_line_stride = frame.video_width as usize / subsampling;
                let src = f.samples(i);
                let dest = unsafe { std::slice::from_raw_parts_mut(frame.p_data[i] as *mut u8, frame.data_len[i] as usize) };
                for y in 0..height {
                    let src_row = &src[y * src_line_stride..(y + 1) * src_line_stride];
                    let dest_row = &mut dest[y * dest_line_stride..y * dest_line_stride + src_line_stride];
                    dest_row.copy_from_slice(src_row);
                }
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
