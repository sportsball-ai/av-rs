use super::{XcoderHardware, XcoderHardwareFrame};
use scopeguard::{guard, ScopeGuard};
use snafu::Snafu;
use std::mem;
use xcoder_quadra_sys as sys;

#[derive(Clone, Debug)]
pub struct XcoderScalerConfig {
    pub hardware: XcoderHardware,
    pub width: i32,
    pub height: i32,
    pub bit_depth: u8,
}

#[derive(Debug, Snafu)]
pub enum XcoderScalerError {
    #[snafu(display("unable to allocate device session context"))]
    UnableToAllocateDeviceSessionContext,
    #[snafu(display("error {operation} (code = {code})"))]
    Unknown { code: sys::ni_retcode_t, operation: &'static str },
}

type Result<T> = std::result::Result<T, XcoderScalerError>;

pub struct XcoderScaler {
    session: *mut sys::ni_session_context_t,
    did_initialize: bool,
    config: XcoderScalerConfig,
}

unsafe impl Send for XcoderScaler {}

impl XcoderScaler {
    pub fn new(config: XcoderScalerConfig) -> Result<Self> {
        unsafe {
            let session = sys::ni_device_session_context_alloc_init();
            if session.is_null() {
                return Err(XcoderScalerError::UnableToAllocateDeviceSessionContext);
            }
            let mut session = guard(session, |session| {
                sys::ni_device_session_context_free(session);
            });

            (**session).hw_id = config.hardware.id;
            (**session).sender_handle = config.hardware.device_handle;
            (**session).device_type = sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER as _;
            (**session).scaler_operation = sys::_ni_scaler_opcode_NI_SCALER_OPCODE_SCALE;

            let code = sys::ni_device_session_open(*session, sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderScalerError::Unknown {
                    code,
                    operation: "opening device session",
                });
            }

            Ok(Self {
                session: ScopeGuard::into_inner(session),
                did_initialize: false,
                config,
            })
        }
    }

    pub fn scale(&mut self, f: &XcoderHardwareFrame) -> Result<XcoderHardwareFrame> {
        let pixel_format_in = if f.surface().bit_depth == 2 { sys::GC620_I010_ } else { sys::GC620_I420_ };
        let pixel_format_out = if self.config.bit_depth == 10 { sys::GC620_I010_ } else { sys::GC620_I420_ };
        unsafe {
            let frame_in = **f;
            // Granting an exception here. This structure is valid in zeroed memory state and there is no
            // officially sanctioned way to initialize it otherwise.
            #[allow(clippy::disallowed_methods)]
            let mut frame_out: sys::ni_frame_t = mem::zeroed();
            frame_out.pts = frame_in.pts;

            let code = sys::ni_frame_buffer_alloc_hwenc(&mut frame_out as _, self.config.width, self.config.height, 0);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderScalerError::Unknown {
                    code,
                    operation: "allocating frame",
                });
            }
            let data_io_out = sys::ni_session_data_io_t {
                data: sys::_ni_session_data_io__bindgen_ty_1 { frame: frame_out },
            };
            let mut data_io_out = guard(data_io_out, |mut data_io| {
                sys::ni_frame_buffer_free(&mut data_io.data.frame);
            });

            if !self.did_initialize {
                // Allocate the output frame pool.
                let code = sys::ni_device_alloc_frame(
                    self.session,
                    self.config.width as _,
                    self.config.height as _,
                    pixel_format_out,
                    (sys::NI_SCALER_FLAG_IO | sys::NI_SCALER_FLAG_PC) as _,
                    0,
                    0,
                    0,
                    0,
                    4, // pool size
                    0,
                    sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER,
                );
                if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                    return Err(XcoderScalerError::Unknown {
                        code,
                        operation: "allocating scaler output frame pool",
                    });
                }

                self.did_initialize = true;
            }

            // Allocate the input frame.
            let code = sys::ni_device_alloc_frame(
                self.session,
                frame_in.video_width as _,
                frame_in.video_height as _,
                pixel_format_in,
                0,
                0,
                0,
                0,
                0,
                f.surface().ui32nodeAddress as _,
                f.surface().ui16FrameIdx as _,
                sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER,
            );
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderScalerError::Unknown {
                    code,
                    operation: "allocating scaler input frame",
                });
            }

            // Allocate the output frame.
            let code = sys::ni_device_alloc_frame(
                self.session,
                self.config.width as _,
                self.config.height as _,
                pixel_format_out,
                sys::NI_SCALER_FLAG_IO as _,
                0,
                0,
                0,
                0,
                0,
                -1,
                sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER,
            );
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderScalerError::Unknown {
                    code,
                    operation: "allocating scaler output frame",
                });
            }

            let code = sys::ni_device_session_read_hwdesc(self.session, &mut *data_io_out, sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderScalerError::Unknown {
                    code,
                    operation: "reading scaler frame",
                });
            }

            let mut ret = XcoderHardwareFrame::new(ScopeGuard::into_inner(data_io_out));
            ret.surface_mut().ui16width = self.config.width as _;
            ret.surface_mut().ui16height = self.config.height as _;
            ret.surface_mut().bit_depth = if self.config.bit_depth == 10 { 2 } else { 1 };
            Ok(ret)
        }
    }
}

impl Drop for XcoderScaler {
    fn drop(&mut self) {
        unsafe {
            sys::ni_device_session_close(self.session, 1, sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER);
            sys::ni_device_close((*self.session).device_handle);
            sys::ni_device_close((*self.session).blk_io_handle);
            sys::ni_device_session_context_free(self.session);
        }
    }
}

#[cfg(test)]
mod test {
    use super::{
        super::{decoder::read_frames, *},
        *,
    };

    #[test]
    fn test_scaler() {
        let frames = read_frames("src/testdata/smptebars.h264");

        let mut decoder = XcoderDecoder::<XcoderHardwareFrame, _, _>::new(
            XcoderDecoderConfig {
                width: 1280,
                height: 720,
                codec: XcoderDecoderCodec::H264,
                bit_depth: 8,
                fps: 29.97,
                hardware_id: None,
                multicore_joint_mode: false,
                number_of_frame_buffers: None,
            },
            frames,
        )
        .unwrap();

        let mut scaler = XcoderScaler::new(XcoderScalerConfig {
            hardware: decoder.hardware(),
            width: 640,
            height: 360,
            bit_depth: 8,
        })
        .unwrap();

        while let Some(frame) = decoder.read_decoded_frame().unwrap() {
            scaler.scale(&frame).unwrap();
        }
    }
}
