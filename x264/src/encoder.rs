use av_traits::{EncodedVideoFrame, RawVideoFrame, VideoEncoder, VideoEncoderInput, VideoEncoderOutput};
use snafu::Snafu;
use std::mem;
use x264_sys as sys;

#[derive(Debug, Snafu)]
pub enum X264EncoderError {
    Unknown,
}

type Result<T> = core::result::Result<T, X264EncoderError>;

pub struct X264Encoder {
    config: X264EncoderConfig,
    encoder: *mut sys::x264_t,
}

impl Drop for X264Encoder {
    fn drop(&mut self) {
        unsafe {
            sys::x264_encoder_close(self.encoder);
        }
    }
}

#[derive(Clone, Debug)]
pub enum X264EncoderInputFormat {
    Yuv420Planar,
    Yuv444Planar,
}

impl X264EncoderInputFormat {
    fn csp(&self) -> i32 {
        (match self {
            Self::Yuv420Planar => sys::X264_CSP_I420,
            Self::Yuv444Planar => sys::X264_CSP_I444,
        }) as _
    }
}

#[derive(Clone, Debug)]
pub struct X264EncoderConfig {
    pub height: u16,
    pub width: u16,
    pub input_format: X264EncoderInputFormat,
}

impl X264Encoder {
    pub fn new(config: X264EncoderConfig) -> Result<Self> {
        unsafe {
            let mut params: mem::MaybeUninit<sys::x264_param_t> = mem::MaybeUninit::uninit();
            sys::x264_param_default(params.as_mut_ptr());
            let mut params = params.assume_init();

            params.i_csp = config.input_format.csp();
            params.i_width = config.width as _;
            params.i_height = config.height as _;

            let encoder = sys::x264_encoder_open(&mut params as _);
            if encoder.is_null() {
                Err(X264EncoderError::Unknown)
            } else {
                Ok(Self { config, encoder })
            }
        }
    }

    fn do_encode<F, C>(&mut self, pic: Option<sys::x264_picture_t>) -> Result<Option<VideoEncoderOutput<C>>> {
        let mut nals: *mut sys::x264_nal_t = std::ptr::null_mut();
        let mut nal_count = 0;
        unsafe {
            let mut pic_out = {
                let mut pic: mem::MaybeUninit<sys::x264_picture_t> = mem::MaybeUninit::uninit();
                sys::x264_picture_init(pic.as_mut_ptr());
                pic.assume_init()
            };
            let nal_bytes = sys::x264_encoder_encode(
                self.encoder,
                &mut nals as _,
                &mut nal_count as _,
                match pic {
                    Some(mut pic) => &mut pic as _,
                    None => std::ptr::null_mut(),
                },
                &mut pic_out as _,
            );
            if nal_bytes < 0 {
                Err(X264EncoderError::Unknown)
            } else if nal_bytes == 0 {
                Ok(None)
            } else {
                let input: Box<VideoEncoderInput<F, C>> = Box::from_raw(pic_out.opaque as _);
                let nals = std::slice::from_raw_parts(nals, nal_count as _);
                let mut data = Vec::with_capacity(nal_bytes as _);
                for nal in nals {
                    data.extend_from_slice(std::slice::from_raw_parts(nal.p_payload, nal.i_payload as _));
                }
                Ok(Some(VideoEncoderOutput {
                    frame: EncodedVideoFrame { data },
                    context: input.context,
                }))
            }
        }
    }
}

impl<F: RawVideoFrame<u8>, C> VideoEncoder<F, C> for X264Encoder {
    type Error = X264EncoderError;

    fn encode(&mut self, input: VideoEncoderInput<F, C>) -> Result<Option<VideoEncoderOutput<C>>> {
        let mut pic = unsafe {
            let mut pic: mem::MaybeUninit<sys::x264_picture_t> = mem::MaybeUninit::uninit();
            sys::x264_picture_init(pic.as_mut_ptr());
            pic.assume_init()
        };
        let input = Box::new(input);
        let samples = input.frame.samples();
        pic.img.i_csp = self.config.input_format.csp();
        pic.img.i_plane = samples.len() as _;
        for (i, plane) in samples.iter().enumerate() {
            pic.img.plane[i] = plane.as_ptr() as _;
        }
        match self.config.input_format {
            X264EncoderInputFormat::Yuv420Planar => {
                for i in 0..3 {
                    pic.img.i_stride[i] = if i == 0 { self.config.width } else { self.config.width / 2 } as _;
                }
            }
            X264EncoderInputFormat::Yuv444Planar => {
                for i in 0..3 {
                    pic.img.i_stride[i] = self.config.width as _;
                }
            }
        }
        pic.opaque = Box::into_raw(input) as _;
        self.do_encode::<F, C>(Some(pic))
    }

    fn flush(&mut self) -> Result<Option<VideoEncoderOutput<C>>> {
        self.do_encode::<F, C>(None)
    }
}
