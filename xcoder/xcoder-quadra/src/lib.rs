#[cfg(target_os = "linux")]
#[path = ""]
mod linux_impl {
    use snafu::Snafu;
    use std::{ffi::c_uint, ops::Deref};
    use xcoder_quadra_sys as sys;

    pub mod cropper;
    pub mod decoder;
    pub mod encoder;
    pub mod scaler;

    pub use cropper::*;
    pub use decoder::*;
    pub use encoder::*;
    pub use scaler::*;

    #[derive(Debug, Snafu)]
    pub enum XcoderInitError {
        #[snafu(display("error (code = {code})"))]
        Unknown { code: sys::ni_retcode_t },
    }

    #[derive(Debug, Clone)]
    pub struct XcoderHardware {
        pub id: i32,
        pub device_handle: i32,
    }

    pub struct XcoderHardwareFrame {
        data_io: sys::ni_session_data_io_t,
    }

    impl XcoderHardwareFrame {
        pub(crate) unsafe fn new(data_io: sys::ni_session_data_io_t) -> Self {
            Self { data_io }
        }

        pub fn as_data_io_mut_ptr(&mut self) -> *mut sys::ni_session_data_io_t {
            &mut self.data_io as _
        }

        pub fn surface(&self) -> &sys::niFrameSurface1_t {
            unsafe { &*(self.p_data[3] as *const sys::niFrameSurface1_t) }
        }

        unsafe fn surface_mut(&mut self) -> &mut sys::niFrameSurface1_t {
            &mut *(self.p_data[3] as *mut sys::niFrameSurface1_t)
        }
    }

    unsafe impl Send for XcoderHardwareFrame {}
    unsafe impl Sync for XcoderHardwareFrame {}

    impl Deref for XcoderHardwareFrame {
        type Target = sys::ni_frame_t;

        fn deref(&self) -> &Self::Target {
            unsafe { &self.data_io.data.frame }
        }
    }

    impl Drop for XcoderHardwareFrame {
        fn drop(&mut self) {
            unsafe {
                if self.surface().ui16FrameIdx > 0 {
                    sys::ni_hwframe_p2p_buffer_recycle(&mut self.data_io.data.frame as _);
                }
                sys::ni_frame_buffer_free(&mut self.data_io.data.frame as _);
            }
        }
    }

    pub fn init(should_match_rev: bool, timeout_seconds: u32) -> Result<(), XcoderInitError> {
        let code = unsafe { sys::ni_rsrc_init(if should_match_rev { 1 } else { 0 }, timeout_seconds as _) };
        if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
            return Err(XcoderInitError::Unknown { code });
        }
        Ok(())
    }

    pub(crate) fn fps_to_rational(fps: f64) -> (i32, i32) {
        let den = if fps.fract() == 0.0 {
            1000
        } else {
            // a denominator of 1001 for 29.97 or 59.94 is more
            // conventional
            1001
        };
        let num = (fps * den as f64).round() as _;
        (num, den)
    }

    fn alloc_zeroed<T>() -> Box<std::mem::MaybeUninit<T>> {
        use std::alloc::GlobalAlloc as _;
        unsafe {
            let raw = std::alloc::System.alloc_zeroed(std::alloc::Layout::from_size_align_unchecked(
                std::mem::size_of::<T>(),
                std::mem::align_of::<T>(),
            ));
            Box::from_raw(raw as *mut std::mem::MaybeUninit<T>)
        }
    }

    #[enum_repr::EnumRepr(type = "c_uint")]
    #[derive(Clone, Copy, Debug, Eq, PartialEq)]
    pub enum XcoderPixelFormat {
        Yuv420Planar = sys::ni_pix_fmt_t_NI_PIX_FMT_YUV420P,
        Yuv420Planar10BitLittleEndian = sys::ni_pix_fmt_t_NI_PIX_FMT_YUV420P10LE,
        Nv12 = sys::ni_pix_fmt_t_NI_PIX_FMT_NV12,
        P010LittleEndian = sys::ni_pix_fmt_t_NI_PIX_FMT_P010LE,
        /// 32 bit packed
        Rgba = sys::ni_pix_fmt_t_NI_PIX_FMT_RGBA,
        /// 32 bit packed
        Bgra = sys::ni_pix_fmt_t_NI_PIX_FMT_BGRA,
        /// 32 bit packed
        Argb = sys::ni_pix_fmt_t_NI_PIX_FMT_ARGB,
        /// 32 bit packed
        Abgr = sys::ni_pix_fmt_t_NI_PIX_FMT_ABGR,
        /// 32 bit packed
        Bgr0 = sys::ni_pix_fmt_t_NI_PIX_FMT_BGR0,
        /// 24 bit packed
        Bgrp = sys::ni_pix_fmt_t_NI_PIX_FMT_BGRP,
        Nv16 = sys::ni_pix_fmt_t_NI_PIX_FMT_NV16,
        Yuyv422 = sys::ni_pix_fmt_t_NI_PIX_FMT_YUYV422,
        Uyvy422 = sys::ni_pix_fmt_t_NI_PIX_FMT_UYVY422,
        None = sys::ni_pix_fmt_t_NI_PIX_FMT_NONE,
    }

    impl XcoderPixelFormat {
        pub fn ten_bit(&self) -> bool {
            use XcoderPixelFormat::*;
            matches!(self, Yuv420Planar10BitLittleEndian | P010LittleEndian)
        }

        /// Returns how many planes are in the layout. For semi-planar formats, a plane containing multiple
        /// types of data is still considered to be one plane.
        pub fn plane_count(&self) -> usize {
            match self {
                XcoderPixelFormat::Yuv420Planar |
                XcoderPixelFormat::Yuv420Planar10BitLittleEndian => 3,
                XcoderPixelFormat::Nv12 |
                XcoderPixelFormat::Nv16 |
                XcoderPixelFormat::P010LittleEndian => 2,
                XcoderPixelFormat::Rgba |
                XcoderPixelFormat::Bgra |
                XcoderPixelFormat::Argb |
                XcoderPixelFormat::Abgr |
                XcoderPixelFormat::Bgr0 |
                XcoderPixelFormat::Bgrp |
                XcoderPixelFormat::Yuyv422 |
                XcoderPixelFormat::Uyvy422 => 1,
                XcoderPixelFormat::None => 0,
            }
        }

        pub fn strides(&self, width: i32) -> [i32; 3] {
            match self {
                XcoderPixelFormat::Yuv420Planar => [width, width / 2, width / 2],
                XcoderPixelFormat::Yuv420Planar10BitLittleEndian => [width * 2, width, width],
                XcoderPixelFormat::Nv12 => [width, width, 0],
                XcoderPixelFormat::P010LittleEndian => [width * 2, width * 2, 0],
                XcoderPixelFormat::Rgba |
                XcoderPixelFormat::Bgra |
                XcoderPixelFormat::Argb |
                XcoderPixelFormat::Abgr |
                XcoderPixelFormat::Bgr0 => [width * 4, 0, 0],
                XcoderPixelFormat::Bgrp => [width * 3, 0, 0],
                XcoderPixelFormat::Nv16 |
                XcoderPixelFormat::Yuyv422 |
                XcoderPixelFormat::Uyvy422 => [width, width / 2, 0],
                XcoderPixelFormat::None => [0; 3],
            }
        }

        pub fn heights(&self, height: i32) -> [i32; 3] {
            match self {
                XcoderPixelFormat::Yuv420Planar |
                XcoderPixelFormat::Yuv420Planar10BitLittleEndian => [height, height / 2, height / 2],
                XcoderPixelFormat::Nv12 |
                XcoderPixelFormat::P010LittleEndian => [height, height / 2, 0],
                XcoderPixelFormat::Rgba |
                XcoderPixelFormat::Bgra |
                XcoderPixelFormat::Argb |
                XcoderPixelFormat::Abgr |
                XcoderPixelFormat::Bgr0 |
                XcoderPixelFormat::Bgrp => [height, 0, 0],
                XcoderPixelFormat::Nv16 |
                XcoderPixelFormat::Yuyv422 |
                XcoderPixelFormat::Uyvy422 => [height, height, 0],
                XcoderPixelFormat::None => [0; 3],
            }
        }

    }
}

#[cfg(target_os = "linux")]
pub use linux_impl::*;

#[cfg(all(test, target_os = "linux"))]
mod test {
    use super::{decoder::read_frames, *};
    use rayon::prelude::*;

    #[test]
    fn test_fps_to_rational() {
        assert_eq!(fps_to_rational(29.97), (30000, 1001));
        assert_eq!(fps_to_rational(30.0), (30000, 1000));
        assert_eq!(fps_to_rational(59.94), (60000, 1001));
        assert_eq!(fps_to_rational(60.0), (60000, 1000));
    }

    #[test]
    fn test_transcoding() {
        let frames = read_frames("src/testdata/smptebars.h264");
        let expected_frame_count = frames.len();

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
        let mut scaler = XcoderScaler::new(XcoderScalerConfig {
            hardware: decoder.hardware(),
            width: 640,
            height: 360,
            bit_depth: 8,
        })
        .unwrap();

        let mut encoder = XcoderEncoder::new(XcoderEncoderConfig {
            width: 640,
            height: 360,
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
            let frame = cropper
                .crop(
                    &frame.into(),
                    XcoderCrop {
                        x: 0,
                        y: 0,
                        width: 960,
                        height: 540,
                    },
                )
                .unwrap();
            let frame = scaler.scale(&frame).unwrap();
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

        // If you want to inspect the output, uncomment these lines:
        //use std::io::Write;
        //std::fs::File::create("out.h264").unwrap().write_all(&encoded).unwrap()
    }

    #[test]
    fn test_8k_ladder() {
        let frames = read_frames("src/testdata/8k-high-bitrate.h265");

        let mut decoder = XcoderDecoder::new(
            XcoderDecoderConfig {
                width: 7680,
                height: 4320,
                codec: XcoderDecoderCodec::H265,
                bit_depth: 8,
                fps: 24.0,
                hardware_id: None,
                multicore_joint_mode: true,
            },
            frames,
        )
        .unwrap();

        struct Encoding {
            cropper: XcoderCropper,
            scaler: XcoderScaler,
            encoder: XcoderEncoder<()>,
            output: Vec<u8>,
        }

        const OUTPUTS: usize = 10;

        let mut encodings = vec![];
        encodings.resize_with(OUTPUTS, || {
            let cropper = XcoderCropper::new(XcoderCropperConfig { hardware: decoder.hardware() }).unwrap();

            let scaler = XcoderScaler::new(XcoderScalerConfig {
                hardware: decoder.hardware(),
                width: 640,
                height: 360,
                bit_depth: 8,
            })
            .unwrap();

            let encoder = XcoderEncoder::new(XcoderEncoderConfig {
                width: 640,
                height: 360,
                fps: 24.0,
                bitrate: None,
                codec: XcoderEncoderCodec::H264 {
                    profile: None,
                    level_idc: None,
                },
                bit_depth: 8,
                pixel_format: XcoderPixelFormat::Yuv420Planar,
                hardware: Some(decoder.hardware()),
                multicore_joint_mode: true,
            })
            .unwrap();

            Encoding {
                cropper,
                scaler,
                encoder,
                output: vec![],
            }
        });

        let mut frame_number = 0;
        while let Some(frame) = decoder.read_decoded_frame().unwrap() {
            let frame = frame.into();

            encodings
                .par_iter_mut()
                .map(|enc| {
                    let frame = enc
                        .cropper
                        .crop(
                            &frame,
                            XcoderCrop {
                                x: frame_number * 16,
                                y: frame_number * 16,
                                width: (1920.0 + 20.0 * frame_number as f64) as _,
                                height: (1080.0 + 20.0 * frame_number as f64) as _,
                            },
                        )
                        .unwrap();
                    let frame = enc.scaler.scale(&frame).unwrap();
                    if let Some(output) = enc.encoder.encode_hardware_frame((), frame).unwrap() {
                        enc.output.append(&mut output.encoded_frame.expect("frame was not dropped").data);
                    }
                    Ok(())
                })
                .collect::<Result<(), XcoderInitError>>()
                .unwrap();
            frame_number += 1;
        }
        encodings
            .par_iter_mut()
            .map(|enc| {
                while let Some(output) = enc.encoder.flush().unwrap() {
                    enc.output.append(&mut output.encoded_frame.expect("frame was not dropped").data);
                }
                assert!(!enc.output.is_empty());
                Ok(())
            })
            .collect::<Result<(), XcoderInitError>>()
            .unwrap();

        // If you want to inspect the output, uncomment these lines:
        /*
        for i in 0..OUTPUTS {
            use std::io::Write;
            std::fs::File::create(format!("out{:02}.h264", i))
                .unwrap()
                .write_all(&encodings[i].output)
                .unwrap()
        }
        */
    }
}
