use super::{AvcConfig, EncodedVideoFrame, RawVideoFrame, VideoEncoder};
use core::mem;
use snafu::Snafu;
use x264_sys as sys;

#[derive(Debug, Snafu)]
pub enum X264EncoderError {
    Unknown,
}

type Result<T> = core::result::Result<T, X264EncoderError>;

pub struct X264Encoder {
    encoder: *mut sys::x264_t,
}

impl Drop for X264Encoder {
    fn drop(&mut self) {
        unsafe {
            sys::x264_encoder_close(self.encoder);
        }
    }
}

impl X264Encoder {
    pub fn new(_config: AvcConfig) -> Result<Self> {
        unsafe {
            let mut p: mem::MaybeUninit<sys::x264_param_t> = mem::MaybeUninit::uninit();
            sys::x264_param_default(p.as_mut_ptr());
            let mut p = p.assume_init();
            let encoder = sys::x264_encoder_open(&mut p as _);
            if encoder.is_null() {
                Err(X264EncoderError::Unknown)
            } else {
                Ok(Self { encoder })
            }
        }
    }
}

impl VideoEncoder for X264Encoder {
    type Error = X264EncoderError;

    fn encode(&mut self, _frame: RawVideoFrame) -> Result<Option<EncodedVideoFrame>> {
        unimplemented!()
    }

    fn flush(&mut self) -> Result<Option<EncodedVideoFrame>> {
        unimplemented!()
    }
}
