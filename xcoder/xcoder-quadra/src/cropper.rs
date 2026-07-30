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
    #[snafu(display("invalid crop {width}x{height}+{x}+{y} for source dimensions {source_width}x{source_height}"))]
    InvalidCrop {
        x: i32,
        y: i32,
        width: i32,
        height: i32,
        source_width: i32,
        source_height: i32,
    },
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

unsafe impl Send for XcoderCropper {}

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
        let source_width = i32::from(f.surface().ui16width);
        let source_height = i32::from(f.surface().ui16height);
        let valid = crop.x >= 0
            && crop.y >= 0
            && crop.width > 0
            && crop.height > 0
            && crop.x % 2 == 0
            && crop.y % 2 == 0
            && crop.width % 2 == 0
            && crop.height % 2 == 0
            && crop.x.checked_add(crop.width).is_some_and(|right| right <= source_width)
            && crop.y.checked_add(crop.height).is_some_and(|bottom| bottom <= source_height);
        if !valid {
            return Err(XcoderCropperError::InvalidCrop {
                x: crop.x,
                y: crop.y,
                width: crop.width,
                height: crop.height,
                source_width,
                source_height,
            });
        }
        let pixel_format = if f.surface().bit_depth == 2 { sys::GC620_I010_ } else { sys::GC620_I420_ };
        unsafe {
            let frame_in = **f;
            let mut frame_out = mem::MaybeUninit::<sys::ni_frame_t>::zeroed();
            let code = sys::ni_frame_buffer_alloc_hwenc(frame_out.as_mut_ptr(), crop.width, crop.height, 0);
            if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
                return Err(XcoderCropperError::Unknown {
                    code,
                    operation: "allocating frame",
                });
            }
            let frame_out = frame_out.assume_init();
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
                    pixel_format,
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
                pixel_format,
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
                pixel_format,
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

            let mut ret = XcoderHardwareFrame::new(ScopeGuard::into_inner(data_io_out));
            ret.surface_mut().ui16width = crop.width as _;
            ret.surface_mut().ui16height = crop.height as _;
            ret.surface_mut().bit_depth = f.surface().bit_depth;
            Ok(ret)
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
        super::{decoder::read_frames, *},
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
                hardware_id: None,
                multicore_joint_mode: false,
            },
            frames,
        )
        .unwrap();

        let mut cropper = XcoderCropper::new(XcoderCropperConfig { hardware: decoder.hardware() }).unwrap();

        let mut frame_count = 0;
        while let Some(frame) = decoder.read_decoded_frame().unwrap() {
            cropper
                .crop(
                    &frame.into(),
                    XcoderCrop {
                        x: 0,
                        y: 0,
                        width: 1280 - frame_count * 2,
                        height: 720 - frame_count * 2,
                    },
                )
                .unwrap();
            frame_count += 1;
        }
    }
}
