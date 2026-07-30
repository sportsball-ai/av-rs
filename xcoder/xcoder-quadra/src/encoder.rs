use crate::XcoderPixelFormat;

use super::{alloc_zeroed, fps_to_rational, XcoderHardware, XcoderHardwareFrame};
use av_traits::{EncodedFrameType, EncodedVideoFrame, RawVideoFrame, VideoEncoder, VideoEncoderOutput};
use scopeguard::{guard, ScopeGuard};
use snafu::Snafu;
use std::{
    array,
    collections::VecDeque,
    mem::{self, MaybeUninit},
    os::raw::c_void,
    ptr::null_mut,
};
use xcoder_quadra_sys::{self as sys, ni_packet_t, ni_xcoder_params_t};

#[derive(Debug, Snafu)]
pub enum XcoderEncoderError {
    #[snafu(display("invalid encoder dimensions {width}x{height}"))]
    InvalidDimensions { width: u16, height: u16 },
    #[snafu(display("invalid encoder frame rate {fps}"))]
    InvalidFrameRate { fps: f64 },
    #[snafu(display("unsupported encoder bit depth {bit_depth}"))]
    UnsupportedBitDepth { bit_depth: u8 },
    #[snafu(display("pixel format {pixel_format:?} does not support {bit_depth}-bit input"))]
    PixelFormatBitDepthMismatch { pixel_format: XcoderPixelFormat, bit_depth: u8 },
    #[snafu(display("hardware frames require planar YUV420 input, not {pixel_format:?}"))]
    UnsupportedHardwarePixelFormat { pixel_format: XcoderPixelFormat },
    #[snafu(display("bitrate must be greater than zero when rate control is enabled"))]
    InvalidBitrate,
    #[snafu(display("input plane {plane} is {actual} bytes, expected at least {expected} bytes"))]
    InputPlaneTooSmall { plane: usize, actual: usize, expected: usize },
    #[snafu(display("encoded packet is {actual} bytes, smaller than its {metadata} bytes of metadata"))]
    InvalidEncodedPacket { actual: usize, metadata: usize },
    #[snafu(display("hardware frame index {index} is outside the supported range 0..{maximum}"))]
    InvalidHardwareFrameIndex { index: usize, maximum: usize },
    #[snafu(display("hardware frame index {index} is still in use by the encoder"))]
    HardwareFrameStillInUse { index: usize },
    #[snafu(display(
        "hardware frame format {actual_width}x{actual_height} {actual_bit_depth}-bit does not match encoder format {expected_width}x{expected_height} {expected_bit_depth}-bit"
    ))]
    HardwareFrameFormatMismatch {
        actual_width: u16,
        actual_height: u16,
        actual_bit_depth: u8,
        expected_width: u16,
        expected_height: u16,
        expected_bit_depth: u8,
    },
    #[snafu(display("unable to allocate device session context"))]
    UnableToAllocateDeviceSessionContext,
    #[snafu(display("error {operation} (code = {code})"))]
    Unknown { code: sys::ni_retcode_t, operation: &'static str },
}

type Result<T> = std::result::Result<T, XcoderEncoderError>;

struct EncodedFrame {
    data_io: sys::ni_session_data_io_t,
    parameter_sets: Option<Vec<u8>>,
    meta_size: usize,
}

impl EncodedFrame {
    pub fn new(meta_size: usize) -> Result<Self> {
        let packet = unsafe {
            let mut packet = MaybeUninit::<ni_packet_t>::zeroed();
            let code = sys::ni_packet_buffer_alloc(packet.as_mut_ptr(), sys::NI_MAX_TX_SZ as _);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderEncoderError::Unknown {
                    code,
                    operation: "allocating packet buffer",
                });
            }
            packet.assume_init()
        };

        Ok(Self {
            data_io: sys::ni_session_data_io_t {
                data: sys::_ni_session_data_io__bindgen_ty_1 { packet },
            },
            parameter_sets: None,
            meta_size,
        })
    }

    pub fn as_slice(&self) -> Result<&[u8]> {
        let data_len = self.data_io.data.packet.data_len as usize;
        if data_len < self.meta_size {
            return Err(XcoderEncoderError::InvalidEncodedPacket {
                actual: data_len,
                metadata: self.meta_size,
            });
        }
        let data = unsafe { std::slice::from_raw_parts(self.data_io.data.packet.p_data as _, data_len) };
        Ok(&data[self.meta_size..])
    }

    /// When parameter sets are emitted by the encoder, they're provided here.
    pub fn parameter_sets(&self) -> Option<&[u8]> {
        self.parameter_sets.as_deref()
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
    _params: Box<ni_xcoder_params_t>,
    frame_data_strides: [i32; sys::NI_MAX_NUM_DATA_POINTERS as usize],
    frame_data_heights: [i32; sys::NI_MAX_NUM_DATA_POINTERS as usize],
    input_frames: VecDeque<F>,
    output_frames: VecDeque<VideoEncoderOutput<F>>,

    // We have to keep hardware frames alive until the encoder says we can recycle them.
    hardware_frames: Vec<Option<XcoderHardwareFrame>>,
}

unsafe impl<F: Send> Send for XcoderEncoder<F> {}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum XcoderH264Profile {
    High,
    Main,
    Baseline,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum XcoderH265Profile {
    Main,
    Main10,
}

#[derive(Clone, Debug)]
pub enum XcoderEncoderCodec {
    H264 {
        level_idc: Option<i32>,
        profile: Option<XcoderH264Profile>,
    },
    H265 {
        level_idc: Option<i32>,
        profile: Option<XcoderH265Profile>,
    },
}

#[derive(Clone, Debug)]
pub struct XcoderEncoderConfig {
    pub height: u16,
    pub width: u16,
    pub fps: f64,
    pub bitrate: Option<u32>,
    pub codec: XcoderEncoderCodec,
    pub pixel_format: XcoderPixelFormat,
    pub bit_depth: u8,
    pub multicore_joint_mode: bool,

    /// To use the encoder with hardware frames, provide this (e.g. from `XcoderDecoder::hardware`).
    pub hardware: Option<XcoderHardware>,
}

impl<F> XcoderEncoder<F> {
    pub fn new(config: XcoderEncoderConfig) -> Result<Self> {
        if !valid_dimensions(i32::from(config.width), i32::from(config.height))
            || (config.pixel_format.requires_even_dimensions() && (config.width % 2 != 0 || config.height % 2 != 0))
        {
            return Err(XcoderEncoderError::InvalidDimensions {
                width: config.width,
                height: config.height,
            });
        }
        if !matches!(config.bit_depth, 8 | 10) {
            return Err(XcoderEncoderError::UnsupportedBitDepth { bit_depth: config.bit_depth });
        }
        if config.pixel_format.bit_depth() != Some(config.bit_depth) {
            return Err(XcoderEncoderError::PixelFormatBitDepthMismatch {
                pixel_format: config.pixel_format,
                bit_depth: config.bit_depth,
            });
        }
        if config.hardware.is_some()
            && !matches!(
                config.pixel_format,
                XcoderPixelFormat::Yuv420Planar | XcoderPixelFormat::Yuv420Planar10BitLittleEndian
            )
        {
            return Err(XcoderEncoderError::UnsupportedHardwarePixelFormat {
                pixel_format: config.pixel_format,
            });
        }
        if config.bitrate == Some(0) {
            return Err(XcoderEncoderError::InvalidBitrate);
        }
        let (fps_numerator, fps_denominator) = fps_to_rational(config.fps).ok_or(XcoderEncoderError::InvalidFrameRate { fps: config.fps })?;

        unsafe {
            let mut params = alloc_zeroed::<sys::ni_xcoder_params_t>();
            let code = sys::ni_encoder_init_default_params(
                params.as_mut_ptr(),
                fps_numerator,
                fps_denominator,
                // There's nothing special about this default bitrate. It's not used unless
                // `rc.enable_rate_control == 1`, so any valid number will do.
                config.bitrate.unwrap_or(200000) as _,
                config.width as _,
                config.height as _,
                match config.codec {
                    XcoderEncoderCodec::H264 { .. } => sys::_ni_codec_format_NI_CODEC_FORMAT_H264,
                    XcoderEncoderCodec::H265 { .. } => sys::_ni_codec_format_NI_CODEC_FORMAT_H265,
                },
            );
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderEncoderError::Unknown {
                    code,
                    operation: "initializing parameters",
                });
            }
            let mut params = mem::transmute::<Box<MaybeUninit<sys::ni_xcoder_params_t>>, Box<sys::ni_xcoder_params_t>>(params);
            let cfg_enc_params = &mut params.__bindgen_anon_1.cfg_enc_params;
            cfg_enc_params.planar = 1;
            cfg_enc_params.rc.enable_rate_control = if config.bitrate.is_some() { 1 } else { 0 };

            // we don't like b-frames. so we use ipppp...
            cfg_enc_params.gop_preset_index = 9;

            // some formats like mpeg-ts require access unit delimiters
            cfg_enc_params.EnableAUD = 1;

            cfg_enc_params.multicoreJointMode = config.multicore_joint_mode.into();

            match config.codec {
                XcoderEncoderCodec::H264 { level_idc, profile } => {
                    if let Some(level_idc) = level_idc {
                        cfg_enc_params.level_idc = level_idc;
                    }

                    if let Some(profile) = profile {
                        cfg_enc_params.profile = match profile {
                            XcoderH264Profile::Main => 2,
                            XcoderH264Profile::High => {
                                if config.bit_depth > 8 {
                                    5
                                } else {
                                    4
                                }
                            }
                            XcoderH264Profile::Baseline => 1,
                        };
                    }
                }
                XcoderEncoderCodec::H265 { level_idc, profile } => {
                    if let Some(level_idc) = level_idc {
                        cfg_enc_params.level_idc = level_idc;
                    }
                    if let Some(profile) = profile {
                        cfg_enc_params.profile = match profile {
                            XcoderH265Profile::Main => 1,
                            XcoderH265Profile::Main10 => 2,
                        };
                    }
                    if let Some(bitrate) = config.bitrate {
                        // This specifies the size of the VBV (CPB) buffer in milliseconds.
                        // The range of supported values of `vbvBufferSize` is 0, and 10 to 3000. For example 3000 should be set for 3 seconds.
                        // Given a particular bitrate value, the maximum value of vbvBufferSize without leading to insufficient resource error is not documented.
                        // For each bitrate range, we maximize vbvBufferSize value that does not exceed the Quadra encoder's processing capacity by trial and error.
                        cfg_enc_params.rc.vbv_buffer_size = match bitrate {
                            0..=80_000_000 => 3000,
                            80_000_001..=120_000_000 => 2000,
                            120_000_001..=240_000_000 => 1000,
                            _ => 0,
                        }
                    };
                }
            }

            let mut frame_data_strides = [0; sys::NI_MAX_NUM_DATA_POINTERS as usize];
            let mut frame_data_heights = [0; sys::NI_MAX_NUM_DATA_POINTERS as usize];
            sys::ni_get_frame_dim(
                config.width as _,
                config.height as _,
                config.pixel_format.repr(),
                frame_data_strides.as_mut_ptr(),
                frame_data_heights.as_mut_ptr(),
            );
            // libxcoder's ni_get_frame_dim leaves incorrect plane heights for
            // NV16 and does not populate BGRP at all, despite both formats
            // being supported by ni_frame_buffer_alloc_pixfmt.
            match config.pixel_format {
                XcoderPixelFormat::Nv16 => {
                    frame_data_heights[0] = i32::from(config.height);
                    frame_data_heights[1] = i32::from(config.height);
                }
                XcoderPixelFormat::Bgrp => {
                    let aligned_width = (i32::from(config.width) + 31) & !31;
                    frame_data_strides[..3].fill(aligned_width);
                    frame_data_heights[..3].fill(i32::from(config.height));
                }
                _ => {}
            }
            if config.hardware.is_some() {
                params.hwframes = 1;
            }

            let pixel_format = i32::try_from(config.pixel_format.repr()).expect("cast should never fail");

            let session = sys::ni_device_session_context_alloc_init();
            if session.is_null() {
                return Err(XcoderEncoderError::UnableToAllocateDeviceSessionContext);
            }
            let mut session = guard(session, |session| {
                sys::ni_device_session_context_free(session);
            });

            (**session).hw_id = -1;
            if let Some(hw) = &config.hardware {
                (**session).hw_id = hw.id;
                (**session).sender_handle = hw.device_handle;
                (**session).hw_action = sys::ni_codec_hw_actions_NI_CODEC_HW_ENABLE as _;
            }
            (**session).p_session_config = params.as_mut() as *mut sys::ni_xcoder_params_t as *mut c_void;
            (**session).codec_format = match config.codec {
                XcoderEncoderCodec::H264 { .. } => sys::_ni_codec_format_NI_CODEC_FORMAT_H264,
                XcoderEncoderCodec::H265 { .. } => sys::_ni_codec_format_NI_CODEC_FORMAT_H265,
            };
            (**session).pixel_format = pixel_format;
            (**session).src_bit_depth = config.bit_depth as _;
            (**session).src_endian = sys::NI_FRAME_LITTLE_ENDIAN as _;
            (**session).bit_depth_factor = if config.bit_depth > 8 { 2 } else { 1 };

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
                let mut frame = mem::MaybeUninit::<sys::ni_frame_t>::zeroed();
                let code = sys::ni_frame_buffer_alloc_pixfmt(
                    frame.as_mut_ptr(),
                    pixel_format,
                    config.width as _,
                    config.height as _,
                    frame_data_strides.as_mut_ptr(),
                    if matches!(config.codec, XcoderEncoderCodec::H264 { .. }) { 1 } else { 0 },
                    sys::NI_APP_ENC_FRAME_META_DATA_SIZE as _,
                );
                if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                    return Err(XcoderEncoderError::Unknown {
                        code,
                        operation: "allocating frame buffer",
                    });
                }
                let frame = frame.assume_init();
                sys::ni_session_data_io_t {
                    data: sys::_ni_session_data_io__bindgen_ty_1 { frame },
                }
            };
            let frame_data_io = guard(frame_data_io, |mut frame_data_io| {
                sys::ni_frame_buffer_free(&mut frame_data_io.data.frame as _);
            });

            let encoded_frame = EncodedFrame::new((**session).meta_size as usize)?;

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
                _params: params,
                input_frames: VecDeque::new(),
                output_frames: VecDeque::new(),
                hardware_frames: {
                    let size = 1 + sys::NI_MAX_DR_HWDESC_FRAME_INDEX.max(sys::NI_MAX_SR_HWDESC_FRAME_INDEX) as usize;
                    let mut frames = Vec::with_capacity(size);
                    frames.resize_with(size, || None);
                    frames
                },
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
                    if (*self.session).pkt_num == 0 {
                        // If we don't set pkt_num, libxcoder doesn't free up packet buffer
                        // resources and will assert after 6000 frames. This mimics the FFmpeg
                        // patch by setting it to 1 after the first successful read.
                        (*self.session).pkt_num = 1;
                    }
                    if packet.recycle_index > 0 && (packet.recycle_index as usize) < self.hardware_frames.len() {
                        self.hardware_frames[packet.recycle_index as usize] = None;
                    }
                    if packet.pts == 0 && packet.avg_frame_qp == 0 {
                        self.encoded_frame.parameter_sets = Some(self.encoded_frame.as_slice()?.to_vec());
                    } else {
                        let encoded_data = match self.encoded_frame.parameter_sets() {
                            Some(sets) => [sets, self.encoded_frame.as_slice()?].concat(),
                            None => self.encoded_frame.as_slice()?.to_vec(),
                        };
                        self.output_frames.push_back(VideoEncoderOutput {
                            raw_frame: self
                                .input_frames
                                .pop_front()
                                .expect("there should never be more output frames than input frames"),
                            encoded_frame: Some(EncodedVideoFrame {
                                data: encoded_data,
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

    /// Attempts to write the decoded frame to the encoder. If Some is returned, the caller must
    /// try again with the same frame later.
    unsafe fn try_write_frame_data_io(&mut self, f: F, data_io: *mut sys::ni_session_data_io_t) -> Result<Option<F>> {
        let written = sys::ni_device_session_write(self.session, data_io, sys::ni_device_type_t_NI_DEVICE_TYPE_ENCODER);
        if written < 0 {
            return Err(XcoderEncoderError::Unknown {
                code: written,
                operation: "writing",
            });
        }

        if written > 0 {
            self.did_start = true;
            self.frame_data_io_has_next_frame = false;
            self.input_frames.push_back(f);
            Ok(None)
        } else {
            Ok(Some(f))
        }
    }

    pub fn encode_hardware_frame(&mut self, input: F, hw_frame: XcoderHardwareFrame) -> Result<Option<VideoEncoderOutput<F>>> {
        self.encode_hardware_frame_with_type(input, hw_frame, EncodedFrameType::Auto)
    }

    pub fn encode_hardware_frame_with_type(
        &mut self,
        mut input: F,
        mut hw_frame: XcoderHardwareFrame,
        frame_type: EncodedFrameType,
    ) -> Result<Option<VideoEncoderOutput<F>>> {
        let surface = hw_frame.surface();
        let actual_bit_depth = if surface.bit_depth == 2 { 10 } else { 8 };
        if surface.ui16width != self.config.width || surface.ui16height != self.config.height || actual_bit_depth != self.config.bit_depth {
            return Err(XcoderEncoderError::HardwareFrameFormatMismatch {
                actual_width: surface.ui16width,
                actual_height: surface.ui16height,
                actual_bit_depth,
                expected_width: self.config.width,
                expected_height: self.config.height,
                expected_bit_depth: self.config.bit_depth,
            });
        }
        let idx = surface.ui16FrameIdx as usize;
        let maximum = self.hardware_frames.len();
        let slot = self
            .hardware_frames
            .get(idx)
            .ok_or(XcoderEncoderError::InvalidHardwareFrameIndex { index: idx, maximum })?;
        if slot.is_some() {
            return Err(XcoderEncoderError::HardwareFrameStillInUse { index: idx });
        }

        let frame = hw_frame.frame_mut();
        frame.start_of_stream = if self.did_start { 0 } else { 1 };
        frame.extra_data_len = sys::NI_APP_ENC_FRAME_META_DATA_SIZE as _;
        if matches!(frame_type, EncodedFrameType::Key) {
            frame.force_key_frame = 1;
            frame.ni_pict_type = sys::ni_pic_type_t_PIC_TYPE_IDR;
        } else {
            frame.force_key_frame = 0;
            frame.ni_pict_type = 0;
        }

        loop {
            self.try_reading_encoded_frames()?;
            match unsafe { self.try_write_frame_data_io(input, hw_frame.as_data_io_mut_ptr())? } {
                Some(frame) => input = frame,
                None => {
                    self.hardware_frames[idx] = Some(hw_frame);
                    break;
                }
            }
        }
        Ok(self.output_frames.pop_front())
    }

    pub fn flush(&mut self) -> Result<Option<VideoEncoderOutput<F>>> {
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

impl<F: RawVideoFrame<u8>> XcoderEncoder<F> {
    /// Attempts to write the decoded frame to the encoder. If Some is returned, the caller must
    /// try again with the same frame later.
    fn try_write_frame(&mut self, f: F, force_key_frame: bool) -> Result<Option<F>> {
        if !self.frame_data_io_has_next_frame {
            let source_strides = self.config.pixel_format.strides(self.config.width as i32);
            let source_heights = self.config.pixel_format.heights(self.config.height as i32);
            for plane in 0..self.config.pixel_format.plane_count() {
                let actual = f.samples(plane).len();
                let expected = usize::try_from(source_strides[plane])
                    .ok()
                    .and_then(|stride| usize::try_from(source_heights[plane]).ok().map(|height| stride * height))
                    .expect("validated encoder dimensions must produce positive plane sizes");
                if actual < expected {
                    return Err(XcoderEncoderError::InputPlaneTooSmall { plane, actual, expected });
                }
            }

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

            let mut dst_data = [frame.p_data[0], frame.p_data[1], frame.p_data[2]];
            let mut src_data: [*mut u8; 3] = array::from_fn(|i| {
                if i < self.config.pixel_format.plane_count() {
                    f.samples(i).as_ptr() as *mut u8
                } else {
                    null_mut()
                }
            });
            let mut src_strides = source_strides;
            let mut src_heights = source_heights;
            let factor = if self.config.pixel_format.ten_bit() { 2 } else { 1 };
            unsafe {
                sys::ni_copy_frame_data(
                    dst_data.as_mut_ptr(),
                    src_data.as_mut_ptr(),
                    self.config.width as _,
                    self.config.height as _,
                    factor,
                    self.config.pixel_format.repr(),
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

        let data_io = &mut self.frame_data_io as *mut _;
        unsafe { self.try_write_frame_data_io(f, data_io) }
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
        XcoderEncoder::flush(self)
    }
}

#[cfg(test)]
mod test {
    use crate::XcoderPixelFormat;

    use super::*;

    struct TestFrame {
        samples: Vec<Vec<u8>>,
    }

    impl RawVideoFrame<u8> for TestFrame {
        fn samples(&self, plane: usize) -> &[u8] {
            &self.samples[plane]
        }
    }

    fn test_config() -> XcoderEncoderConfig {
        XcoderEncoderConfig {
            width: 1920,
            height: 1080,
            fps: 29.97,
            bitrate: None,
            codec: XcoderEncoderCodec::H264 {
                profile: Some(XcoderH264Profile::Main),
                level_idc: Some(41),
            },
            bit_depth: 8,
            pixel_format: XcoderPixelFormat::Yuv420Planar,
            hardware: None,
            multicore_joint_mode: false,
        }
    }

    #[test]
    fn rejects_invalid_encoder_configuration_before_opening_a_device() {
        let mut config = test_config();
        config.width = 0;
        assert!(matches!(
            XcoderEncoder::<TestFrame>::new(config),
            Err(XcoderEncoderError::InvalidDimensions { .. })
        ));

        let mut config = test_config();
        config.width = 1919;
        assert!(matches!(
            XcoderEncoder::<TestFrame>::new(config),
            Err(XcoderEncoderError::InvalidDimensions { .. })
        ));

        let mut config = test_config();
        config.fps = f64::NAN;
        assert!(matches!(
            XcoderEncoder::<TestFrame>::new(config),
            Err(XcoderEncoderError::InvalidFrameRate { .. })
        ));

        let mut config = test_config();
        config.bit_depth = 10;
        assert!(matches!(
            XcoderEncoder::<TestFrame>::new(config),
            Err(XcoderEncoderError::PixelFormatBitDepthMismatch { .. })
        ));

        let mut config = test_config();
        config.pixel_format = XcoderPixelFormat::Bgra;
        config.hardware = Some(XcoderHardware { id: -1, device_handle: -1 });
        assert!(matches!(
            XcoderEncoder::<TestFrame>::new(config),
            Err(XcoderEncoderError::UnsupportedHardwarePixelFormat { .. })
        ));

        let mut config = test_config();
        config.bitrate = Some(0);
        assert!(matches!(XcoderEncoder::<TestFrame>::new(config), Err(XcoderEncoderError::InvalidBitrate)));
    }

    #[test]
    fn rejects_short_input_planes_before_copying_to_the_sdk() {
        let mut config = test_config();
        config.width = 64;
        config.height = 64;
        let mut encoder = XcoderEncoder::new(config).unwrap();
        let frame = TestFrame {
            samples: vec![vec![0; 64 * 64 - 1], vec![0; 64 * 32], vec![0; 64 * 32]],
        };

        assert!(matches!(
            encoder.encode(frame, EncodedFrameType::Auto),
            Err(XcoderEncoderError::InputPlaneTooSmall {
                plane: 0,
                actual: 4095,
                expected: 4096
            })
        ));
    }

    #[test]
    fn test_video_encoder() {
        let mut encoder = XcoderEncoder::new(test_config()).unwrap();

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
        //std::fs::File::create("tmp.h264").unwrap().write_all(&encoded).unwrap();
    }

    #[test]
    fn test_video_encoder_bgra() {
        let mut encoder = XcoderEncoder::new(XcoderEncoderConfig {
            width: 1920,
            height: 1080,
            fps: 29.97,
            bitrate: None,
            codec: XcoderEncoderCodec::H264 {
                profile: Some(XcoderH264Profile::Main),
                level_idc: Some(41),
            },
            bit_depth: 8,
            pixel_format: XcoderPixelFormat::Bgra,
            hardware: None,
            multicore_joint_mode: false,
        })
        .unwrap();

        let mut encoded = vec![];
        let mut encoded_frames = 0;

        for i in 0..90 {
            let mut bgra = Vec::<[u8; 4]>::with_capacity(1920 * 1080);
            for line in 0..1080 {
                let sample = if line / 12 == i {
                    // add some motion by drawing a line that moves from top to bottom
                    0
                } else {
                    ((line as f64 / 1080.0) * 255.0).round() as u8
                };
                bgra.resize(bgra.len() + 1920, [sample, sample, sample, 255]);
            }
            let frame = TestFrame {
                samples: vec![bytemuck::cast_slice(&bgra).to_vec()],
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
        //std::fs::File::create("tmp.h264").unwrap().write_all(&encoded).unwrap();
    }
}
