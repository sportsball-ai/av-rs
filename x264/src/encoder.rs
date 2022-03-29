use av_traits::{EncodedVideoFrame, RawVideoFrame, VideoEncoder, VideoEncoderOutput};
use snafu::Snafu;
use std::{marker::PhantomData, mem};
use x264_sys as sys;

#[derive(Debug, Snafu)]
pub enum X264EncoderError {
    Unknown,
}

type Result<T> = core::result::Result<T, X264EncoderError>;

pub struct X264Encoder<F> {
    config: X264EncoderConfig,
    encoder: *mut sys::x264_t,
    frame_count: u64,

    // raw frames are sent to the c library and stored there during encoding
    frame: PhantomData<F>,
}

impl<F> Drop for X264Encoder<F> {
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
    pub fps: f64,
    pub input_format: X264EncoderInputFormat,
}

impl<F> X264Encoder<F> {
    pub fn new(config: X264EncoderConfig) -> Result<Self> {
        unsafe {
            let mut params: mem::MaybeUninit<sys::x264_param_t> = mem::MaybeUninit::uninit();
            sys::x264_param_default(params.as_mut_ptr());
            let mut params = params.assume_init();

            // disable b-frames to minimize latency and keep things simple
            params.i_bframe = 0;

            params.i_csp = config.input_format.csp();
            params.i_width = config.width as _;
            params.i_height = config.height as _;
            params.i_fps_den = if config.fps.fract() == 0.0 {
                1000
            } else {
                // a denominator of 1001 for 29.97 or 59.94 is more
                // conventional
                1001
            };
            params.i_fps_num = (config.fps * params.i_fps_den as f64).round() as _;
            params.i_timebase_num = params.i_fps_den;
            params.i_timebase_den = params.i_fps_num;

            let encoder = sys::x264_encoder_open(&mut params as _);
            if encoder.is_null() {
                Err(X264EncoderError::Unknown)
            } else {
                Ok(Self {
                    config,
                    encoder,
                    frame_count: 0,
                    frame: PhantomData,
                })
            }
        }
    }

    fn do_encode(&mut self, mut pic: Option<sys::x264_picture_t>) -> Result<Option<VideoEncoderOutput<F>>> {
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
                match &mut pic {
                    Some(pic) => pic as _,
                    None => std::ptr::null_mut(),
                },
                &mut pic_out as _,
            );
            match nal_bytes {
                0 => Ok(None),
                nal_bytes if nal_bytes < 0 => Err(X264EncoderError::Unknown),
                nal_bytes => {
                    let input: Box<F> = Box::from_raw(pic_out.opaque as _);
                    let nals = std::slice::from_raw_parts(nals, nal_count as _);
                    let mut data = Vec::with_capacity(nal_bytes as _);
                    for nal in nals {
                        data.extend_from_slice(std::slice::from_raw_parts(nal.p_payload, nal.i_payload as _));
                    }
                    Ok(Some(VideoEncoderOutput {
                        raw_frame: *input,
                        encoded_frame: EncodedVideoFrame {
                            data,
                            is_keyframe: pic_out.b_keyframe != 0,
                        },
                    }))
                }
            }
        }
    }
}

impl<F: RawVideoFrame<u8>> VideoEncoder for X264Encoder<F> {
    type Error = X264EncoderError;
    type RawVideoFrame = F;

    fn encode(&mut self, input: F) -> Result<Option<VideoEncoderOutput<F>>> {
        let mut pic = unsafe {
            let mut pic: mem::MaybeUninit<sys::x264_picture_t> = mem::MaybeUninit::uninit();
            sys::x264_picture_init(pic.as_mut_ptr());
            pic.assume_init()
        };
        let input = Box::new(input);
        pic.img.i_csp = self.config.input_format.csp();
        match self.config.input_format {
            X264EncoderInputFormat::Yuv420Planar => {
                pic.img.i_plane = 3;
                for i in 0..3 {
                    pic.img.plane[i] = input.samples(i).as_ptr() as _;
                    pic.img.i_stride[i] = if i == 0 { self.config.width } else { self.config.width / 2 } as _;
                }
            }
            X264EncoderInputFormat::Yuv444Planar => {
                pic.img.i_plane = 3;
                for i in 0..3 {
                    pic.img.plane[i] = input.samples(i).as_ptr() as _;
                    pic.img.i_stride[i] = self.config.width as _;
                }
            }
        }
        pic.opaque = Box::into_raw(input) as _;
        pic.i_pts = self.frame_count as _;
        self.frame_count += 1;
        self.do_encode(Some(pic))
    }

    fn flush(&mut self) -> Result<Option<VideoEncoderOutput<F>>> {
        self.do_encode(None)
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
        let mut encoder = X264Encoder::new(X264EncoderConfig {
            width: 1920,
            height: 1080,
            fps: 29.97,
            input_format: X264EncoderInputFormat::Yuv420Planar,
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
            if let Some(mut output) = encoder.encode(frame).unwrap() {
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
    }
}
