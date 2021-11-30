use super::sys;
use core_foundation::{result, CFType, OSStatus};
use core_media::{BlockBuffer, SampleBuffer, VideoFormatDescription};
use core_video::ImageBuffer;
use std::{pin::Pin, sync::mpsc};

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

    pub fn decode_frame(&mut self, frame_data: &[u8], format_desc: &VideoFormatDescription) -> Result<ImageBuffer, OSStatus> {
        let (tx, rx) = mpsc::channel();
        let mut cb: Pin<Box<Callback>> = Box::pin(Box::new(move |status, image_buffer| {
            tx.send(if image_buffer.is_null() {
                Err(status.into())
            } else {
                result(status.into()).map(|_| unsafe { ImageBuffer::with_cf_type_ref(image_buffer as _) })
            })
            .unwrap();
        }));
        result(
            unsafe {
                let block_buffer = BlockBuffer::with_memory_block(frame_data)?;
                let sample_buffer = SampleBuffer::new(&block_buffer, Some(format_desc), 1, Some(&[frame_data.len()]))?;
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
        rx.try_recv().unwrap()
    }
}

#[cfg(test)]
mod test {
    use super::*;
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
}
