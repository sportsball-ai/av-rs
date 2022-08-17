use super::{XcoderHardware, XcoderHardwareFrame};
use scopeguard::{guard, ScopeGuard};
use snafu::Snafu;
use std::mem;
use xcoder_quadra_sys as sys;

#[derive(Clone, Debug)]
pub struct XcoderCropperConfig {
    pub hardware: XcoderHardware,
}

#[derive(Debug, Snafu)]
pub enum XcoderCropperError {
    #[snafu(display("unable to allocate device session context"))]
    UnableToAllocateDeviceSessionContext,
    #[snafu(display("error {operation} (code = {code})"))]
    Unknown { code: sys::ni_retcode_t, operation: &'static str },
}

type Result<T> = std::result::Result<T, XcoderCropperError>;

pub struct XcoderCropper {
    session: *mut sys::ni_session_context_t,
    did_initialize: bool,
}

#[derive(Clone, Debug)]
pub struct XcoderCrop {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
}

impl XcoderCropper {
    pub fn new(config: XcoderCropperConfig) -> Result<Self> {
        unsafe {
            let session = sys::ni_device_session_context_alloc_init();
            if session.is_null() {
                return Err(XcoderCropperError::UnableToAllocateDeviceSessionContext);
            }
            let mut session = guard(session, |session| {
                sys::ni_device_session_context_free(session);
            });

            (**session).hw_id = config.hardware.id;
            (**session).sender_handle = config.hardware.device_handle;
            (**session).device_type = sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER as _;
            (**session).scaler_operation = sys::_ni_scaler_opcode_NI_SCALER_OPCODE_CROP;

            let code = sys::ni_device_session_open(*session, sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderCropperError::Unknown {
                    code,
                    operation: "opening device session",
                });
            }

            Ok(Self {
                session: ScopeGuard::into_inner(session),
                did_initialize: false,
            })
        }
    }

    pub fn crop(&mut self, f: &XcoderHardwareFrame, crop: XcoderCrop) -> Result<XcoderHardwareFrame> {
        const PIXEL_FORMAT: i32 = 0x103; // GC620_I420;
        unsafe {
            let frame_in = **f;
            let mut frame_out: sys::ni_frame_t = mem::zeroed();
            let code = sys::ni_frame_buffer_alloc_hwenc(&mut frame_out as _, crop.width, crop.height, 0);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderCropperError::Unknown {
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
                    frame_in.video_width as _,
                    frame_in.video_height as _,
                    PIXEL_FORMAT,
                    (sys::NI_SCALER_FLAG_IO | sys::NI_SCALER_FLAG_PC) as _,
                    0,
                    0,
                    0,
                    0,
                    1, // pool size
                    0,
                    sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER,
                );
                if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                    return Err(XcoderCropperError::Unknown {
                        code,
                        operation: "allocating cropper output frame pool",
                    });
                }

                self.did_initialize = true;
            }

            // Allocate the input frame.
            let code = sys::ni_device_alloc_frame(
                self.session,
                frame_in.video_width as _,
                frame_in.video_height as _,
                PIXEL_FORMAT,
                0,
                crop.width,
                crop.height,
                crop.x,
                crop.y,
                f.surface().ui32nodeAddress as _,
                f.surface().ui16FrameIdx as _,
                sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER,
            );
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderCropperError::Unknown {
                    code,
                    operation: "allocating cropper input frame",
                });
            }

            // Allocate the output frame.
            let code = sys::ni_device_alloc_frame(
                self.session,
                crop.width as _,
                crop.height as _,
                PIXEL_FORMAT,
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
                return Err(XcoderCropperError::Unknown {
                    code,
                    operation: "allocating cropper output frame",
                });
            }

            let code = sys::ni_device_session_read_hwdesc(self.session, &mut *data_io_out, sys::ni_device_type_t_NI_DEVICE_TYPE_SCALER);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderCropperError::Unknown {
                    code,
                    operation: "reading cropper frame",
                });
            }

            Ok(XcoderHardwareFrame::new(ScopeGuard::into_inner(data_io_out)))
        }
    }
}

impl Drop for XcoderCropper {
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
        super::{decoder::test::read_frames, *},
        *,
    };

    #[test]
    fn test_cropper() {
        let frames = read_frames("src/testdata/smptebars.h264");

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

        let mut cropper = XcoderCropper::new(XcoderCropperConfig { hardware: decoder.hardware() }).unwrap();

        let mut frame_count = 0;
        while !decoder.is_finished() {
            if let Some(frame) = decoder.try_read_decoded_frame().unwrap() {
                cropper
                    .crop(
                        &frame.into(),
                        XcoderCrop {
                            x: 0,
                            y: 0,
                            width: 1280 - frame_count,
                            height: 720 - frame_count,
                        },
                    )
                    .unwrap();
                frame_count += 1;
            }
        }
    }
}
