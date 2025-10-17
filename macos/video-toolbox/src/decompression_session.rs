use super::sys;
use core_foundation::{result, CFType, Dictionary, OSStatus};
use core_media::{BlockBuffer, SampleBuffer, VideoFormatDescription};
use core_video::ImageBuffer;
use std::{
    pin::Pin,
    sync::{Arc, Mutex},
};

pub struct DecompressionSession(sys::VTDecompressionSessionRef);
core_foundation::trait_impls!(DecompressionSession);

type Callback = Box<dyn FnMut(sys::OSStatus, sys::CVImageBufferRef)>;

impl DecompressionSession {
    pub fn new(format_desc: &VideoFormatDescription) -> Result<Self, OSStatus> {
        unsafe extern "C" fn callback(
            _decompression_output_ref_con: *mut std::os::raw::c_void,
            source_frame_ref_con: *mut std::os::raw::c_void,
            status: sys::OSStatus,
            _info_flags: sys::VTDecodeInfoFlags,
            image_buffer: sys::CVImageBufferRef,
            _presentation_time_stamp: sys::CMTime,
            _presentation_duration: sys::CMTime,
        ) {
            (*(source_frame_ref_con as *mut Callback))(status, image_buffer)
        }
        let callback_record = sys::VTDecompressionOutputCallbackRecord {
            decompressionOutputCallback: Some(callback),
            decompressionOutputRefCon: std::ptr::null_mut(),
        };
        let mut ret = std::ptr::null_mut();
        result(
            unsafe {
                sys::VTDecompressionSessionCreate(
                    std::ptr::null_mut(),
                    format_desc.cf_type_ref() as _,
                    std::ptr::null(),
                    std::ptr::null(),
                    &callback_record as _,
                    &mut ret as _,
                )
            }
            .into(),
        )?;
        Ok(Self(ret))
    }

    pub fn new_with_destination_image_buffer_attributes(
        format_desc: &VideoFormatDescription,
        destination_image_buffer_attributes: Dictionary,
    ) -> Result<Self, OSStatus> {
        unsafe extern "C" fn callback(
            _decompression_output_ref_con: *mut std::os::raw::c_void,
            source_frame_ref_con: *mut std::os::raw::c_void,
            status: sys::OSStatus,
            _info_flags: sys::VTDecodeInfoFlags,
            image_buffer: sys::CVImageBufferRef,
            _presentation_time_stamp: sys::CMTime,
            _presentation_duration: sys::CMTime,
        ) {
            (*(source_frame_ref_con as *mut Callback))(status, image_buffer)
        }
        let callback_record = sys::VTDecompressionOutputCallbackRecord {
            decompressionOutputCallback: Some(callback),
            decompressionOutputRefCon: std::ptr::null_mut(),
        };
        let mut ret = std::ptr::null_mut();
        result(
            unsafe {
                sys::VTDecompressionSessionCreate(
                    std::ptr::null_mut(),
                    format_desc.cf_type_ref() as _,
                    std::ptr::null(),
                    destination_image_buffer_attributes.cf_type_ref() as _,
                    &callback_record as _,
                    &mut ret as _,
                )
            }
            .into(),
        )?;
        Ok(Self(ret))
    }

    pub fn decode_frame(&mut self, frame_data: &[u8], format_desc: &VideoFormatDescription) -> Result<ImageBuffer, OSStatus> {
        let image_mutex = Arc::new(Mutex::new(None));
        let cb_image_mutex = image_mutex.clone();
        let mut cb: Pin<Box<Callback>> = Box::pin(Box::new(move |status, image_buffer| {
            // SAFETY: Panicking is not allowed across an FFI boundary. If you add code that may panic here
            // then you must wrap it in `std::panic::catch_unwind`.
            if let Ok(mut lock) = cb_image_mutex.try_lock() {
                *lock = Some(if image_buffer.is_null() {
                    Err(status.into())
                } else {
                    result(status.into()).map(|_| unsafe { ImageBuffer::from_get_rule(image_buffer as _) })
                });
            }
        }));
        result(
            unsafe {
                let block_buffer = BlockBuffer::with_memory_block(frame_data)?;
                let sample_buffer = SampleBuffer::new(&block_buffer, Some(format_desc.into()), 1, Some(&[frame_data.len()]))?;
                sys::VTDecompressionSessionDecodeFrame(
                    self.0,
                    sample_buffer.cf_type_ref() as _,
                    0,
                    &mut *cb as *mut Callback as _,
                    std::ptr::null_mut(),
                )
            }
            .into(),
        )?;
        let mut mutex_lock = image_mutex.lock().expect("lock shouldn't be poisoned");
        mutex_lock.take().expect("mutex should be populated")
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use core_foundation::{MutableDictionary, Number};
    use core_media::VideoFormatDescription;
    use std::{fs::File, io::Read};

    #[test]
    fn test_decompression_session() {
        let mut f = File::open("src/testdata/smptebars.h264").unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();

        let nalus: Vec<_> = h264::iterate_annex_b(&buf).collect();
        let format_desc = VideoFormatDescription::with_h264_parameter_sets(&[nalus[0], nalus[1]], 4).unwrap();
        let mut sess = DecompressionSession::new(&format_desc).unwrap();

        // This file is encoded as exactly one NALU per frame.
        for nalu in &nalus[3..10] {
            let mut frame_data = vec![0, 0, (nalu.len() / 256) as u8, nalu.len() as u8];
            frame_data.extend_from_slice(nalu);
            sess.decode_frame(&frame_data, &format_desc).unwrap();
        }
    }

    #[test]
    fn test_decompression_session_with_destination_image_buffer_attributes() {
        let mut f = File::open("src/testdata/smptebars.h264").unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();

        let nalus: Vec<_> = h264::iterate_annex_b(&buf).collect();
        let format_desc = VideoFormatDescription::with_h264_parameter_sets(&[nalus[0], nalus[1]], 4).unwrap();

        let mut destination_image_buffer_attributes = MutableDictionary::new_cf_type();
        // Set the pixel format to BGRA
        unsafe {
            let key = sys::kCVPixelBufferPixelFormatTypeKey as _;
            let value = Number::from(sys::kCVPixelFormatType_32BGRA).cf_type_ref();
            destination_image_buffer_attributes.set_value(key, value);
        }
        let mut sess = DecompressionSession::new_with_destination_image_buffer_attributes(&format_desc, destination_image_buffer_attributes.into()).unwrap();

        // This file is encoded as exactly one NALU per frame.
        for nalu in &nalus[3..10] {
            let mut frame_data = vec![0, 0, (nalu.len() / 256) as u8, nalu.len() as u8];
            frame_data.extend_from_slice(nalu);
            let decoded_frame = sess.decode_frame(&frame_data, &format_desc).unwrap();
            let decoded_pixel_format = decoded_frame.pixel_buffer().pixel_format_type();
            let decoded_width = decoded_frame.pixel_buffer().width();
            let decoded_height = decoded_frame.pixel_buffer().height();

            assert_eq!(decoded_width, 1280);
            assert_eq!(decoded_height, 720);
            assert_eq!(decoded_pixel_format, sys::kCVPixelFormatType_32BGRA);
        }
    }
}
