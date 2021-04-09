use super::AVCaptureOutput;
use core_foundation::CFType;
use core_media::{sys::CMSampleBufferRef, SampleBuffer};
use std::ffi::c_void;

pub mod sys {
    use std::ffi::c_void;

    extern "C" {
        pub fn avrs_new_av_capture_video_data_output() -> *const c_void;
        pub fn avrs_new_av_capture_video_data_output_set_sample_buffer_delegate(output: *const c_void, delegate: *const c_void);
        pub fn avrs_new_av_capture_video_data_output_sample_buffer_delegate(implementation: *const c_void) -> *const c_void;
        pub fn avrs_new_av_capture_video_data_output_set_video_settings(output: *const c_void, pixel_format: u32);
    }
}

pub struct AVCaptureVideoDataOutput(*const c_void);

pub trait AVCaptureVideoDataOutputSampleBufferDelegateProtocol {
    fn did_output_sample_buffer(&self, _buffer: SampleBuffer) {}
}

pub struct AVCaptureVideoDataOutputSampleBufferDelegate {
    raw: *const c_void,
    _native: Box<Box<dyn AVCaptureVideoDataOutputSampleBufferDelegateProtocol + Send>>,
}

unsafe impl Send for AVCaptureVideoDataOutputSampleBufferDelegate {}

impl AVCaptureVideoDataOutputSampleBufferDelegate {
    pub fn new<D: 'static + AVCaptureVideoDataOutputSampleBufferDelegateProtocol + Send>(delegate: D) -> AVCaptureVideoDataOutputSampleBufferDelegate {
        let delegate: Box<Box<dyn AVCaptureVideoDataOutputSampleBufferDelegateProtocol + Send>> = Box::new(Box::new(delegate));
        Self {
            raw: unsafe {
                sys::avrs_new_av_capture_video_data_output_sample_buffer_delegate(
                    &*delegate as *const Box<dyn AVCaptureVideoDataOutputSampleBufferDelegateProtocol + Send> as _,
                )
            },
            _native: delegate,
        }
    }
}

impl Drop for AVCaptureVideoDataOutputSampleBufferDelegate {
    fn drop(&mut self) {
        unsafe { super::sys::avrs_avf_release_object(self.raw) }
    }
}

#[no_mangle]
unsafe extern "C" fn avrs_av_capture_video_data_output_sample_buffer_delegate_did_output(
    implementation: *const Box<dyn AVCaptureVideoDataOutputSampleBufferDelegateProtocol>,
    buffer: CMSampleBufferRef,
) {
    let implementation = &*implementation;
    implementation.did_output_sample_buffer(SampleBuffer::with_cf_type_ref(buffer as _));
}

pub struct AVCaptureVideoDataOutputVideoSettings {
    pub pixel_format_type: Option<u32>,
}

impl AVCaptureVideoDataOutput {
    pub fn new() -> Self {
        Self(unsafe { sys::avrs_new_av_capture_video_data_output() })
    }

    /// Sets the sample buffer delegate. The output only retains a weak reference to this delegate,
    /// so you must keep it alive for as long as you need it to function.
    pub fn set_sample_buffer_delegate(&self, delegate: &AVCaptureVideoDataOutputSampleBufferDelegate) {
        unsafe {
            sys::avrs_new_av_capture_video_data_output_set_sample_buffer_delegate(self.0, delegate.raw);
        }
    }

    pub fn set_video_settings(&self, settings: &AVCaptureVideoDataOutputVideoSettings) {
        unsafe { sys::avrs_new_av_capture_video_data_output_set_video_settings(self.0, settings.pixel_format_type.unwrap_or(0)) }
    }
}

impl AVCaptureOutput for &AVCaptureVideoDataOutput {
    fn raw(&self) -> *const c_void {
        self.0
    }
}

impl Drop for AVCaptureVideoDataOutput {
    fn drop(&mut self) {
        unsafe { super::sys::avrs_avf_release_object(self.0) }
    }
}
