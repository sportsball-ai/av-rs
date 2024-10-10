use super::{alloc_zeroed, fps_to_rational, XcoderHardware};
use scopeguard::{guard, ScopeGuard};
use snafu::{Error, Snafu};
use std::{
    marker::PhantomData,
    mem::{self, MaybeUninit},
    os::raw::c_void,
};
use xcoder_quadra_sys::{self as sys};

#[derive(Clone, Copy, Debug)]
pub enum XcoderDecoderCodec {
    H264,
    H265,
}

#[derive(Clone, Debug)]
pub struct XcoderDecoderConfig {
    pub width: i32,
    pub height: i32,
    pub bit_depth: u8,
    pub fps: f64,
    pub codec: XcoderDecoderCodec,
    pub hardware_id: Option<i32>,
    pub multicore_joint_mode: bool,
    /// Only used with software frames, ignored for hardware frames
    /// If set, determines the initial count of frame buffers
    /// Note that:
    /// 1) This does not allocate the space for the frame buffer itself (which could be ~50MB for an 8K 8bit frame),
    ///     that only happens on first usage of a specific buffer
    /// 2) This is an initial amount. The NETINT codebase will grow the buffer as needed
    pub frame_buffer: Option<usize>,
}

pub struct XcoderDecoderInputFrame {
    pub data: Vec<u8>,
    pub dts: u64,
    pub pts: u64,
}

impl XcoderDecoderInputFrame {
    fn to_data_io<E: Error>(&self, pos: i64, width: i32, height: i32, is_start_of_stream: bool) -> Result<DataIo, XcoderDecoderError<E>> {
        unsafe {
            // Granting an exception here. This structure is valid in zeroed memory state and there is no
            // officially sanctioned way to initialize it otherwise. Plus the netint SDK zeroes this structure itself.
            #[allow(clippy::disallowed_methods)]
            let mut packet: sys::ni_packet_t = mem::zeroed();
            packet.dts = self.dts as _;
            packet.pts = self.pts as _;
            packet.pos = pos;
            packet.start_of_stream = if is_start_of_stream { 1 } else { 0 };
            packet.video_width = width as _;
            packet.video_height = height as _;
            packet.data_len = self.data.len() as _;

            let code = sys::ni_packet_buffer_alloc(&mut packet as _, self.data.len() as _);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderDecoderError::Unknown {
                    code,
                    operation: "allocating packet buffer",
                });
            }
            let mut prev_size = 0;
            let copied = sys::ni_packet_copy(
                packet.p_data,
                self.data.as_ptr() as _,
                self.data.len() as _,
                self.data.as_ptr() as _,
                &mut prev_size as _,
            );
            if copied < self.data.len() as i32 {
                return Err(XcoderDecoderError::UnableToCopyToPacketBuffer {
                    actual: copied as _,
                    expected: self.data.len(),
                });
            }

            Ok(DataIo {
                inner: sys::ni_session_data_io_t {
                    data: sys::_ni_session_data_io__bindgen_ty_1 { packet },
                },
            })
        }
    }
}

pub trait XcoderDecoderInput<E>: Iterator<Item = Result<XcoderDecoderInputFrame, E>> {}

impl<T, E> XcoderDecoderInput<E> for T where T: Iterator<Item = Result<XcoderDecoderInputFrame, E>> {}

pub struct XcoderDecoder<F, I, E> {
    config: XcoderDecoderConfig,
    input: I,
    pos: i64,
    did_start: bool,
    did_flush: bool,
    session: *mut sys::ni_session_context_t,
    _params: Box<sys::ni_xcoder_params_t>,
    eos_received: bool,
    next_packet_data_io: Option<DataIo>,
    next_decoded_frame: F,
    _input_error_type: PhantomData<E>,
}

unsafe impl<F, I, E> Send for XcoderDecoder<F, I, E> {}

#[derive(Debug, Snafu)]
pub enum XcoderDecoderError<E: Error + 'static> {
    #[snafu(context(false), display("input error: {source}"))]
    InputError { source: E },
    #[snafu(display("unsupported bit depth"))]
    UnsupportedBitDepth,
    #[snafu(display("unable to allocate device session context"))]
    UnableToAllocateDeviceSessionContext,
    #[snafu(display("unable to copy to packet buffer (copied {actual} bytes, expected {expected}))"))]
    UnableToCopyToPacketBuffer { expected: usize, actual: usize },
    #[snafu(display("error {operation} (code = {code})"))]
    Unknown { code: sys::ni_retcode_t, operation: &'static str },
}

struct DataIo {
    inner: sys::ni_session_data_io_t,
}

impl DataIo {
    pub fn as_mut_ptr(&mut self) -> *mut sys::ni_session_data_io_t {
        &mut self.inner as _
    }
}

impl Drop for DataIo {
    fn drop(&mut self) {
        unsafe {
            sys::ni_packet_buffer_free(&mut self.inner.data.packet as _);
        }
    }
}

pub trait XcoderDecodedFrame {
    const HARDWARE: bool;

    /// Downloads a frame from the current session
    ///
    /// # Safety
    ///
    /// Expects session to be a valid pointer
    unsafe fn from_session<E>(session: *mut sys::ni_session_context_t, width: i32, height: i32) -> Result<Self, XcoderDecoderError<E>>
    where
        Self: Sized,
        E: Error;

    fn as_data_io_mut_ptr(&mut self) -> *mut sys::ni_session_data_io_t;

    fn is_end_of_stream(&self) -> bool;
}

impl<F: XcoderDecodedFrame, E: Error, I: XcoderDecoderInput<E>> XcoderDecoder<F, I, E> {
    pub fn new<II>(config: XcoderDecoderConfig, input: II) -> Result<Self, XcoderDecoderError<E>>
    where
        II: IntoIterator<IntoIter = I>,
    {
        let (fps_numerator, fps_denominator) = fps_to_rational(config.fps);

        unsafe {
            let mut params = alloc_zeroed();
            let code = sys::ni_decoder_init_default_params(params.as_mut_ptr(), fps_numerator, fps_denominator, 0, config.width, config.height);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderDecoderError::Unknown {
                    code,
                    operation: "initializing parameters",
                });
            }
            let mut params = mem::transmute::<Box<MaybeUninit<sys::ni_xcoder_params_t>>, Box<sys::ni_xcoder_params_t>>(params);
            params.__bindgen_anon_1.dec_input_params.hwframes = 1;
            params.__bindgen_anon_1.dec_input_params.mcmode = config.multicore_joint_mode.into();

            let session = sys::ni_device_session_context_alloc_init();
            if session.is_null() {
                return Err(XcoderDecoderError::UnableToAllocateDeviceSessionContext);
            }
            let mut session = guard(session, |session| {
                sys::ni_device_session_context_free(session);
            });

            (**session).hw_id = config.hardware_id.unwrap_or(-1);
            if F::HARDWARE {
                (**session).hw_action = sys::ni_codec_hw_actions_NI_CODEC_HW_ENABLE as _;
            }
            (**session).p_session_config = params.as_mut() as *mut sys::ni_xcoder_params_t as *mut c_void;
            (**session).codec_format = match config.codec {
                XcoderDecoderCodec::H264 => sys::_ni_codec_format_NI_CODEC_FORMAT_H264,
                XcoderDecoderCodec::H265 => sys::_ni_codec_format_NI_CODEC_FORMAT_H265,
            };
            (**session).bit_depth_factor = match config.bit_depth {
                8 => 1,
                10 => 2,
                _ => return Err(XcoderDecoderError::UnsupportedBitDepth),
            };

            let code = sys::ni_device_session_open(*session, sys::ni_device_type_t_NI_DEVICE_TYPE_DECODER);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderDecoderError::Unknown {
                    code,
                    operation: "opening device session",
                });
            }
            let eos_received = guard(false, |eos_received| {
                sys::ni_device_session_close(*session, if eos_received { 1 } else { 0 }, sys::ni_device_type_t_NI_DEVICE_TYPE_DECODER);
            });

            if !F::HARDWARE {
                if let Some(frame_buffer) = config.frame_buffer {
                    // no drop guard needed here, the `(*self.session).dec_fme_buf_pool`
                    // is cleaned up by `ni_device_session_close`, which calls `ni_decoder_session_close`

                    let code = sys::ni_dec_fme_buffer_pool_initialize(
                        *session,
                        frame_buffer as i32,
                        config.width,
                        config.height,
                        ((**session).codec_format == sys::_ni_codec_format_NI_CODEC_FORMAT_H264).into(),
                        (**session).bit_depth_factor,
                    );

                    if code != 0 {
                        return Err(XcoderDecoderError::Unknown {
                            code,
                            operation: "ni_dec_fme_buffer_pool_initialize",
                        });
                    }
                }
            }

            Ok(Self {
                next_decoded_frame: F::from_session(*session, config.width, config.height)?,
                config,
                did_start: false,
                did_flush: false,
                next_packet_data_io: None,
                input: input.into_iter(),
                _input_error_type: PhantomData,
                pos: 0,
                _params: params,
                eos_received: ScopeGuard::into_inner(eos_received),
                session: ScopeGuard::into_inner(session),
            })
        }
    }

    pub fn hardware(&self) -> XcoderHardware {
        unsafe {
            XcoderHardware {
                id: (*self.session).hw_id,
                device_handle: (*self.session).device_handle,
            }
        }
    }

    pub fn is_finished(&self) -> bool {
        self.eos_received
    }

    /// Reads a decoded frame. Returns None once the decoder is finished.
    pub fn read_decoded_frame(&mut self) -> Result<Option<F>, XcoderDecoderError<E>> {
        while !self.is_finished() {
            if let Some(frame) = self.try_read_decoded_frame()? {
                return Ok(Some(frame));
            }
        }
        Ok(None)
    }

    /// Tries to read a decoded frame. If none is returned and `is_finished` returns false, the caller should try again later.
    fn try_read_decoded_frame(&mut self) -> Result<Option<F>, XcoderDecoderError<E>> {
        if self.is_finished() {
            return Ok(None);
        }

        unsafe {
            // try reading an encoded frame from the input
            {
                if self.next_packet_data_io.is_none() {
                    if let Some(frame) = self.input.next().transpose()? {
                        let mut data_io = frame.to_data_io(self.pos, self.config.width, self.config.height, !self.did_start)?;
                        let data_len = (*data_io.as_mut_ptr()).data.packet.data_len;
                        self.did_start = true;
                        self.pos += data_len as i64;
                        self.next_packet_data_io = Some(data_io);
                    } else if !self.did_flush {
                        let code = sys::ni_device_session_flush(self.session, sys::ni_device_type_t_NI_DEVICE_TYPE_DECODER);
                        if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                            return Err(XcoderDecoderError::Unknown {
                                code,
                                operation: "flushing decoder",
                            });
                        }
                        self.did_flush = true;
                    }
                }
            }

            // try writing an encoded frame to the decoder
            {
                if let Some(data_io) = self.next_packet_data_io.as_mut() {
                    let written = sys::ni_device_session_write(self.session, data_io.as_mut_ptr(), sys::ni_device_type_t_NI_DEVICE_TYPE_DECODER);
                    if written < 0 {
                        return Err(XcoderDecoderError::Unknown {
                            code: written,
                            operation: "writing packet to decoder session",
                        });
                    } else if (written as u32) > 0 {
                        self.next_packet_data_io = None;
                    }
                }
            }

            // try reading a decoded frame from the decoder
            {
                let code = if F::HARDWARE {
                    sys::ni_device_session_read_hwdesc(
                        self.session,
                        self.next_decoded_frame.as_data_io_mut_ptr(),
                        sys::ni_device_type_t_NI_DEVICE_TYPE_DECODER,
                    )
                } else {
                    sys::ni_device_session_read(
                        self.session,
                        self.next_decoded_frame.as_data_io_mut_ptr(),
                        sys::ni_device_type_t_NI_DEVICE_TYPE_DECODER,
                    )
                };
                if code < 0 {
                    return Err(XcoderDecoderError::Unknown {
                        code,
                        operation: "reading decoded frame",
                    });
                }
                self.eos_received = self.next_decoded_frame.is_end_of_stream();
                if code > 0 {
                    let frame = mem::replace(
                        &mut self.next_decoded_frame,
                        F::from_session(self.session, self.config.width, self.config.height)?,
                    );
                    return Ok(Some(frame));
                }
            }
        }

        Ok(None)
    }
}

impl<I, E, O> Drop for XcoderDecoder<I, E, O> {
    fn drop(&mut self) {
        unsafe {
            // do NOT clean up `(*self.session).dec_fme_buf_pool` with `ni_dec_fme_buffer_pool_free`,
            // as it breaks `ni_device_session_close`

            sys::ni_device_session_close(
                self.session,
                if self.eos_received { 1 } else { 0 },
                sys::ni_device_type_t_NI_DEVICE_TYPE_DECODER,
            );
            sys::ni_device_session_context_free(self.session);
        }
    }
}

#[cfg(test)]
/// Reads frames from a raw .h264 or .h265 file.
pub fn read_frames(path: &str) -> Vec<Result<XcoderDecoderInputFrame, std::io::Error>> {
    use std::io::Read;

    let mut f = std::fs::File::open(path).unwrap();
    let mut buf = Vec::new();
    f.read_to_end(&mut buf).unwrap();

    let nalus: Vec<_> = h264::iterate_annex_b(&buf).collect();

    let mut ret = vec![];
    let mut buffer = vec![];
    let mut h264_counter = h264::AccessUnitCounter::new();
    let mut h265_counter = h265::AccessUnitCounter::new();
    for nalu in nalus {
        let is_new_frame = if path.contains(".h264") {
            let before = h264_counter.count();
            h264_counter.count_nalu(nalu).unwrap();
            h264_counter.count() != before
        } else {
            let before = h265_counter.count();
            h265_counter.count_nalu(nalu).unwrap();
            h265_counter.count() != before
        };
        if is_new_frame && !buffer.is_empty() {
            ret.push(Ok(XcoderDecoderInputFrame {
                data: mem::take(&mut buffer),
                pts: ret.len() as _,
                dts: ret.len() as _,
            }));
        }
        buffer.extend_from_slice(&[0, 0, 0, 1]);
        buffer.extend_from_slice(nalu);
    }
    if !buffer.is_empty() {
        ret.push(Ok(XcoderDecoderInputFrame {
            data: mem::take(&mut buffer),
            pts: ret.len() as _,
            dts: ret.len() as _,
        }));
    }
    ret
}

#[cfg(test)]
mod test {
    use av_traits::VideoEncoder;

    use super::{super::*, *};

    #[test]
    fn test_decoder() {
        let frames = read_frames("src/testdata/smptebars.h264");
        let expected_frame_count = frames.len();
        let mut decoder = XcoderDecoder::<XcoderHardwareFrame, _, _>::new(
            XcoderDecoderConfig {
                width: 1280,
                height: 720,
                codec: XcoderDecoderCodec::H264,
                bit_depth: 8,
                fps: 29.97,
                hardware_id: None,
                multicore_joint_mode: false,
                frame_buffer: None,
            },
            frames,
        )
        .unwrap();

        let mut frame_count = 0;
        while decoder.read_decoded_frame().unwrap().is_some() {
            frame_count += 1;
        }
        assert_eq!(frame_count, expected_frame_count);
    }

    #[test]
    fn test_decoder_encoder_hardware_interop() {
        let frames = read_frames("src/testdata/smptebars.h264");
        let expected_frame_count = frames.len();

        let mut decoder = XcoderDecoder::<XcoderHardwareFrame, _, _>::new(
            XcoderDecoderConfig {
                width: 1280,
                height: 720,
                codec: XcoderDecoderCodec::H264,
                bit_depth: 8,
                fps: 29.97,
                hardware_id: None,
                multicore_joint_mode: false,
                frame_buffer: None,
            },
            frames,
        )
        .unwrap();

        let mut encoder = XcoderEncoder::new(XcoderEncoderConfig {
            width: 1280,
            height: 720,
            fps: 29.97,
            bitrate: None,
            codec: XcoderEncoderCodec::H264 {
                profile: None,
                level_idc: None,
            },
            bit_depth: 8,
            pixel_format: XcoderPixelFormat::Yuv420Planar,
            hardware: Some(decoder.hardware()),
            multicore_joint_mode: false,
        })
        .unwrap();

        let mut encoded_frames = 0;
        let mut encoded = vec![];

        while let Some(frame) = decoder.read_decoded_frame().unwrap() {
            if let Some(output) = encoder.encode_hardware_frame((), frame).unwrap() {
                encoded.append(&mut output.encoded_frame.expect("frame was not dropped").data);
                encoded_frames += 1;
            }
        }
        while let Some(output) = encoder.flush().unwrap() {
            encoded.append(&mut output.encoded_frame.expect("frame was not dropped").data);
            encoded_frames += 1;
        }

        assert_eq!(encoded_frames, expected_frame_count);
        assert!(encoded.len() > 5000);
    }

    #[test]
    fn test_decoder_sw_framebuffer() {
        let frames = read_frames("src/testdata/smptebars.h264");
        let expected_frame_count = frames.len();
        let mut decoder = XcoderDecoder::<XcoderSoftwareFrame, _, _>::new(
            XcoderDecoderConfig {
                width: 1280,
                height: 720,
                codec: XcoderDecoderCodec::H264,
                bit_depth: 8,
                fps: 29.97,
                hardware_id: None,
                multicore_joint_mode: false,
                frame_buffer: Some(4),
            },
            frames,
        )
        .unwrap();

        let mut frame_count = 0;
        while decoder.read_decoded_frame().unwrap().is_some() {
            frame_count += 1;
        }
        assert_eq!(frame_count, expected_frame_count);
    }

    #[test]
    fn test_decoder_encoder_sw() {
        let original_width = 1280;
        let original_height = 720;

        let frames = read_frames("src/testdata/smptebars.h264");
        let expected_frame_count = frames.len();

        let mut decoder = XcoderDecoder::<XcoderSoftwareFrame, _, _>::new(
            XcoderDecoderConfig {
                width: original_width,
                height: original_height,
                codec: XcoderDecoderCodec::H264,
                bit_depth: 8,
                fps: 29.97,
                hardware_id: None,
                multicore_joint_mode: false,
                frame_buffer: None,
            },
            frames,
        )
        .unwrap();

        let mut encoder = XcoderEncoder::new(XcoderEncoderConfig {
            width: original_width as u16,
            height: original_height as u16,
            fps: 29.97,
            bitrate: None,
            codec: XcoderEncoderCodec::H265 {
                profile: None,
                level_idc: None,
            },
            bit_depth: 8,
            pixel_format: XcoderPixelFormat::Yuv420Planar,
            hardware: None,
            multicore_joint_mode: false,
        })
        .unwrap();

        let mut frame_count = 0;
        let mut encoded = vec![];

        while let Some(frame) = decoder.read_decoded_frame().unwrap() {
            let xcoder_sw_frame = frame;

            if let Some(output) = encoder.encode(xcoder_sw_frame, av_traits::EncodedFrameType::Auto).unwrap() {
                encoded.append(&mut output.encoded_frame.expect("frame was not dropped").data);
                frame_count += 1;
            }
        }
        while let Some(output) = encoder.flush().unwrap() {
            encoded.append(&mut output.encoded_frame.expect("frame was not dropped").data);
            frame_count += 1;
        }

        // To inspect the output, uncomment these lines:
        // std::io::Write::write_all(&mut std::fs::File::create("cropped.h265").unwrap(), &encoded).unwrap();

        assert_eq!(frame_count, expected_frame_count);
    }
}
