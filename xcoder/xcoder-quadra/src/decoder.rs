use super::{fps_to_rational, XcoderHardware};
use scopeguard::{guard, ScopeGuard};
use snafu::{Error, Snafu};
use std::{marker::PhantomData, mem, ops::Deref};
use xcoder_quadra_sys as sys;

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
}

pub struct XcoderDecoderInputFrame {
    pub data: Vec<u8>,
    pub dts: u64,
    pub pts: u64,
}

impl XcoderDecoderInputFrame {
    fn to_data_io<E: Error>(&self, pos: i64, width: i32, height: i32, is_start_of_stream: bool) -> Result<DataIo, XcoderDecoderError<E>> {
        unsafe {
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

pub struct XcoderDecodedFrame {
    data_io: sys::ni_session_data_io_t,
}

impl XcoderDecodedFrame {
    unsafe fn new_hardware_frame<E: Error>(session: *mut sys::ni_session_context_t, width: i32, height: i32) -> Result<Self, XcoderDecoderError<E>> {
        let mut frame: sys::ni_frame_t = mem::zeroed();
        let code = sys::ni_frame_buffer_alloc(
            &mut frame as _,
            width,
            height,
            if (*session).codec_format == sys::_ni_codec_format_NI_CODEC_FORMAT_H264 {
                1
            } else {
                0
            },
            1,
            (*session).bit_depth_factor,
            3,
            1,
        );
        if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
            return Err(XcoderDecoderError::Unknown {
                code,
                operation: "allocating frame",
            });
        }
        Ok(Self {
            data_io: sys::ni_session_data_io_t {
                data: sys::_ni_session_data_io__bindgen_ty_1 { frame },
            },
        })
    }

    pub fn as_data_io_mut_ptr(&mut self) -> *mut sys::ni_session_data_io_t {
        &mut self.data_io as _
    }
}

unsafe impl Send for XcoderDecodedFrame {}
unsafe impl Sync for XcoderDecodedFrame {}

impl Deref for XcoderDecodedFrame {
    type Target = sys::ni_frame_t;

    fn deref(&self) -> &Self::Target {
        unsafe { &self.data_io.data.frame }
    }
}

impl Drop for XcoderDecodedFrame {
    fn drop(&mut self) {
        unsafe {
            sys::ni_hwframe_p2p_buffer_recycle(&mut self.data_io.data.frame as _);
            sys::ni_frame_buffer_free(&mut self.data_io.data.frame as _);
        }
    }
}

pub trait XcoderDecoderInput<E>: Iterator<Item = Result<XcoderDecoderInputFrame, E>> {}

impl<T, E> XcoderDecoderInput<E> for T where T: Iterator<Item = Result<XcoderDecoderInputFrame, E>> {}

pub struct XcoderDecoder<I, E> {
    config: XcoderDecoderConfig,
    input: I,
    pos: i64,
    did_start: bool,
    did_flush: bool,
    session: *mut sys::ni_session_context_t,
    _params: Box<sys::ni_xcoder_params_t>,
    eos_received: bool,
    next_packet_data_io: Option<DataIo>,
    next_decoded_frame: XcoderDecodedFrame,
    _input_error_type: PhantomData<E>,
}

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

impl<E: Error, I: XcoderDecoderInput<E>> XcoderDecoder<I, E> {
    pub fn new<II>(config: XcoderDecoderConfig, input: II) -> Result<Self, XcoderDecoderError<E>>
    where
        II: IntoIterator<IntoIter = I>,
    {
        let (fps_denominator, fps_numerator) = fps_to_rational(config.fps);

        unsafe {
            let mut params: sys::ni_xcoder_params_t = mem::zeroed();
            let code = sys::ni_decoder_init_default_params(&mut params as _, fps_numerator, fps_denominator, 0, config.width, config.height);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderDecoderError::Unknown {
                    code,
                    operation: "initializing parameters",
                });
            }
            params.__bindgen_anon_1.dec_input_params.hwframes = 1;
            let params = Box::new(params);

            let session = sys::ni_device_session_context_alloc_init();
            if session.is_null() {
                return Err(XcoderDecoderError::UnableToAllocateDeviceSessionContext);
            }
            let mut session = guard(session, |session| {
                sys::ni_device_session_context_free(session);
            });

            (**session).hw_id = -1;
            (**session).hw_action = sys::ni_codec_hw_actions_NI_CODEC_HW_ENABLE as _;
            (**session).p_session_config = &*params as *const sys::ni_xcoder_params_t as _;
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

            Ok(Self {
                next_decoded_frame: XcoderDecodedFrame::new_hardware_frame(*session, config.width, config.height)?,
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

    /// Tries to read a decoded frame. If none is returned and `is_finished` returns false, the caller should try again later.
    pub fn try_read_decoded_frame(&mut self) -> Result<Option<XcoderDecodedFrame>, XcoderDecoderError<E>> {
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
                let code = sys::ni_device_session_read_hwdesc(
                    self.session,
                    self.next_decoded_frame.as_data_io_mut_ptr(),
                    sys::ni_device_type_t_NI_DEVICE_TYPE_DECODER,
                );
                if code < 0 {
                    return Err(XcoderDecoderError::Unknown {
                        code,
                        operation: "reading decoded frame",
                    });
                }
                self.eos_received = (*self.next_decoded_frame).end_of_stream != 0;
                if code > 0 {
                    let frame = mem::replace(
                        &mut self.next_decoded_frame,
                        XcoderDecodedFrame::new_hardware_frame(self.session, self.config.width, self.config.height)?,
                    );
                    return Ok(Some(frame));
                }
            }
        }

        Ok(None)
    }
}

impl<I, E> Drop for XcoderDecoder<I, E> {
    fn drop(&mut self) {
        unsafe {
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
mod test {
    use super::{super::*, *};
    use std::io::{self, Read};

    /// Reads frames from a raw .h264 file.
    fn read_frames(path: &str) -> Vec<Result<XcoderDecoderInputFrame, io::Error>> {
        let mut f = std::fs::File::open(path).unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();

        let nalus: Vec<_> = h264::iterate_annex_b(&buf).collect();

        let mut ret = vec![];
        let mut buffer = vec![];
        for nalu in nalus {
            buffer.extend_from_slice(&[0, 0, 0, 1]);
            buffer.extend_from_slice(nalu);
            let nalu_type = nalu[0] & h264::NAL_UNIT_TYPE_MASK;
            if nalu_type == 5 || nalu_type == 1 {
                ret.push(Ok(XcoderDecoderInputFrame {
                    data: mem::replace(&mut buffer, vec![]),
                    pts: ret.len() as _,
                    dts: ret.len() as _,
                }));
            }
        }
        ret
    }

    #[test]
    fn test_decoder() {
        let frames = read_frames("src/testdata/smptebars.h264");
        let expected_frame_count = frames.len();
        let mut decoder = XcoderDecoder::new(
            XcoderDecoderConfig {
                width: 1280,
                height: 720,
                codec: XcoderDecoderCodec::H264,
                bit_depth: 8,
                fps: 29.97,
            },
            frames,
        )
        .unwrap();

        let mut frame_count = 0;
        while !decoder.is_finished() {
            if decoder.try_read_decoded_frame().unwrap().is_some() {
                frame_count += 1;
            }
        }
        assert_eq!(frame_count, expected_frame_count);
    }

    #[test]
    fn test_decoder_encoder_hardware_interop() {
        let frames = read_frames("src/testdata/smptebars.h264");
        let expected_frame_count = frames.len();

        let mut decoder = XcoderDecoder::new(
            XcoderDecoderConfig {
                width: 1280,
                height: 720,
                codec: XcoderDecoderCodec::H264,
                bit_depth: 8,
                fps: 29.97,
            },
            frames,
        )
        .unwrap();

        let mut encoder = XcoderEncoder::new(XcoderEncoderConfig {
            width: 1280,
            height: 720,
            fps: 29.97,
            bitrate: None,
            codec: XcoderEncoderCodec::H264,
            hardware: Some(decoder.hardware()),
        })
        .unwrap();

        let mut encoded_frames = 0;
        let mut encoded = vec![];

        while !decoder.is_finished() {
            if let Some(mut frame) = decoder.try_read_decoded_frame().unwrap() {
                if let Some(mut output) = unsafe { encoder.encode_data_io((), frame.as_data_io_mut_ptr()).unwrap() } {
                    encoded.append(&mut output.encoded_frame.data);
                    encoded_frames += 1;
                }
            }
        }
        while let Some(mut output) = encoder.flush().unwrap() {
            encoded.append(&mut output.encoded_frame.data);
            encoded_frames += 1;
        }

        assert_eq!(encoded_frames, expected_frame_count);
        assert!(encoded.len() > 5000);
    }
}
