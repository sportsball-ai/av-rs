use super::sys;
use av_traits::EncodedFrameType;
use core_foundation::{result, Boolean, CFType, Dictionary, MutableDictionary, OSStatus, StringRef};
use core_media::{SampleBuffer, VideoCodecType};
use core_video::ImageBuffer;
use std::{
    marker::{PhantomData, PhantomPinned},
    pin::Pin,
    sync::mpsc,
};

struct CallbackContext<C> {
    frames: mpsc::Sender<Result<CompressionSessionOutputFrame<C>, OSStatus>>,
    _pinned: PhantomPinned,
}

pub struct CompressionSessionOutputFrame<C> {
    /// The encoded frame, or `None` if it was dropped.
    pub sample_buffer: Option<SampleBuffer>,
    pub context: C,
}

struct CompressionSessionInner(sys::VTCompressionSessionRef);
core_foundation::trait_impls!(CompressionSessionInner);

pub struct CompressionSession<C: Send> {
    inner: CompressionSessionInner,
    frames: mpsc::Receiver<Result<CompressionSessionOutputFrame<C>, OSStatus>>,
    _callback_context: Pin<Box<CallbackContext<C>>>,
    _frame_context: PhantomData<C>,
}

#[derive(Debug)]
pub struct CompressionSessionConfig {
    pub width: i32,
    pub height: i32,
    pub codec_type: VideoCodecType,
    pub encoder_specification: Option<Dictionary>,
}

impl<C: Send> CompressionSession<C> {
    pub fn new(config: CompressionSessionConfig) -> Result<Self, OSStatus> {
        let (tx, rx) = mpsc::channel();

        let callback_context = Box::pin(CallbackContext {
            frames: tx,
            _pinned: PhantomPinned,
        });

        unsafe extern "C" fn callback<C>(
            output_callback_ref_con: *mut std::os::raw::c_void,
            source_frame_ref_con: *mut std::os::raw::c_void,
            status: sys::OSStatus,
            _info_flags: sys::VTDecodeInfoFlags,
            sample_buffer: sys::CMSampleBufferRef,
        ) {
            let ctx = &*(output_callback_ref_con as *mut CallbackContext<C>);
            let frame_context = *Box::<C>::from_raw(source_frame_ref_con as *mut C);
            let _ = ctx.frames.send(result(status.into()).map(|_| CompressionSessionOutputFrame {
                sample_buffer: if sample_buffer.is_null() {
                    None
                } else {
                    Some(SampleBuffer::from_get_rule(sample_buffer as _))
                },
                context: frame_context,
            }));
        }

        let mut sess = std::ptr::null_mut();
        result(
            unsafe {
                sys::VTCompressionSessionCreate(
                    std::ptr::null_mut(),
                    config.width,
                    config.height,
                    config.codec_type.into(),
                    match &config.encoder_specification {
                        Some(dict) => dict.cf_type_ref() as _,
                        None => std::ptr::null(),
                    },
                    std::ptr::null(),
                    std::ptr::null(),
                    Some(callback::<C>),
                    &*callback_context as *const _ as *mut _,
                    &mut sess as _,
                )
            }
            .into(),
        )?;
        Ok(Self {
            inner: CompressionSessionInner(sess),
            frames: rx,
            _callback_context: callback_context,
            _frame_context: PhantomData,
        })
    }

    pub fn set_property<V: CFType>(&mut self, key: sys::CFStringRef, value: V) -> Result<(), OSStatus> {
        unsafe { result(sys::VTSessionSetProperty(self.inner.0 as _, key as _, value.cf_type_ref()).into()) }
    }

    pub fn set_property_str<V: CFType>(&mut self, key: &'static str, value: V) -> Result<(), OSStatus> {
        unsafe { result(sys::VTSessionSetProperty(self.inner.0 as _, StringRef::from_static(key).cf_type_ref() as _, value.cf_type_ref()).into()) }
    }

    pub fn prepare_to_encode_frames(&mut self) -> Result<(), OSStatus> {
        unsafe { result(sys::VTCompressionSessionPrepareToEncodeFrames(self.inner.0).into()) }
    }

    pub fn frames(&self) -> &mpsc::Receiver<Result<CompressionSessionOutputFrame<C>, OSStatus>> {
        &self.frames
    }

    pub fn encode_frame(
        &mut self,
        image_buffer: ImageBuffer,
        presentation_time: sys::CMTime,
        context: C,
        frame_type: EncodedFrameType,
    ) -> Result<(), OSStatus> {
        let context = Box::new(context);
        result(
            unsafe {
                let mut frame_options = MutableDictionary::new_cf_type();
                match frame_type {
                    EncodedFrameType::Key => {
                        frame_options.set_value(sys::kVTEncodeFrameOptionKey_ForceKeyFrame as _, Boolean::from(true).cf_type_ref() as _);
                    }
                    EncodedFrameType::Auto => {}
                };

                sys::VTCompressionSessionEncodeFrame(
                    self.inner.0,
                    image_buffer.cf_type_ref() as _,
                    presentation_time,
                    sys::kCMTimeInvalid,
                    frame_options.cf_type_ref() as _,
                    Box::into_raw(context) as _,
                    std::ptr::null_mut(),
                )
            }
            .into(),
        )
    }

    pub fn flush(&mut self) -> Result<(), OSStatus> {
        result(unsafe { sys::VTCompressionSessionCompleteFrames(self.inner.0, sys::kCMTimeInvalid) }.into())
    }
}

impl<C: Send> Drop for CompressionSession<C> {
    fn drop(&mut self) {
        let _ = self.flush();
        unsafe {
            sys::VTCompressionSessionInvalidate(self.inner.0);
        }
    }
}

#[cfg(test)]
mod test {
    use super::{super::DecompressionSession, *};
    use core_media::VideoFormatDescription;
    use std::{fs::File, io::Read};

    #[test]
    fn test_compression_session() {
        let mut f = File::open("src/testdata/smptebars.h264").unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();

        let nalus: Vec<_> = h264::iterate_annex_b(&buf).collect();
        let format_desc = VideoFormatDescription::with_h264_parameter_sets(&[nalus[0], nalus[1]], 4).unwrap();
        let mut decompression_session = DecompressionSession::new(&format_desc).unwrap();
        let mut compression_session = CompressionSession::new(CompressionSessionConfig {
            width: 1280,
            height: 720,
            codec_type: VideoCodecType::H264,
            encoder_specification: None,
        })
        .unwrap();

        // This file is encoded as exactly one NALU per frame.
        let mut frames_sent = 0;
        for nalu in &nalus[3..10] {
            let mut frame_data = vec![0, 0, (nalu.len() / 256) as u8, nalu.len() as u8];
            frame_data.extend_from_slice(nalu);
            let image_buffer = decompression_session.decode_frame(&frame_data, &format_desc).unwrap();
            let presentation_time = unsafe { sys::CMTimeMake(frames_sent, 30) };
            frames_sent += 1;
            compression_session
                .encode_frame(image_buffer, presentation_time, (), EncodedFrameType::Auto)
                .unwrap();
        }
        compression_session.flush().unwrap();

        let mut frame_count = 0;
        while let Ok(frame) = compression_session.frames().try_recv() {
            frame.unwrap();
            frame_count += 1;
        }
        assert_eq!(frame_count, frames_sent);
    }

    #[test]
    fn test_create_cm_time() {
        let time = unsafe { sys::CMTimeMake(20, 1000) };
        assert_eq!(time.value as i64, 20);
        assert_eq!(time.timescale as i32, 1000);
        assert_eq!(time.flags, sys::kCMTimeFlags_Valid);
        assert_eq!(time.epoch as i64, 0);
    }
}
