use super::{AVCaptureInput, AVMediaType};
use foundation::NSError;
use std::ffi::c_void;

pub mod sys {
    use std::{ffi::c_void, os::raw::c_int};

    extern "C" {
        pub fn avrs_default_av_capture_device(media_type: c_int) -> *const c_void;
        pub fn avrs_avf_request_av_capture_device_access(media_type: c_int) -> bool;
        pub fn avrs_avf_av_capture_device_input_from_device(device: *const c_void, err: *mut *const c_void) -> *const c_void;
        pub fn avrs_avf_av_capture_device_configure(device: *const c_void, fps: f64) -> bool;
    }
}

pub struct AVCaptureDevice(*const c_void);

pub struct AVCaptureDeviceConfig {
    pub fps: Option<f64>,
}

impl AVCaptureDevice {
    pub fn default_with_media_type(media_type: AVMediaType) -> Option<Self> {
        let raw = unsafe { sys::avrs_default_av_capture_device(media_type.as_sys()) };
        if raw.is_null() {
            None
        } else {
            Some(Self(raw))
        }
    }

    /// Requests access for the given media type if needed and blocks until it is granted or
    /// denied. Returns true if access is granted and false otherwise.
    pub fn request_access(media_type: AVMediaType) -> bool {
        unsafe { sys::avrs_avf_request_av_capture_device_access(media_type.as_sys()) }
    }

    pub fn configure(&self, config: AVCaptureDeviceConfig) -> bool {
        unsafe { sys::avrs_avf_av_capture_device_configure(self.0, config.fps.unwrap_or(0.0)) }
    }
}

impl Drop for AVCaptureDevice {
    fn drop(&mut self) {
        unsafe { super::sys::avrs_avf_release_object(self.0) }
    }
}

pub struct AVCaptureDeviceInput(*const c_void);

impl AVCaptureDeviceInput {
    pub fn new(device: &AVCaptureDevice) -> Result<Self, NSError> {
        unsafe {
            let mut err: *const c_void = std::ptr::null();
            let input = sys::avrs_avf_av_capture_device_input_from_device(device.0, &mut err as _);
            if err.is_null() {
                Ok(Self(input))
            } else {
                Err(NSError::from_raw(err))
            }
        }
    }
}

impl AVCaptureInput for &AVCaptureDeviceInput {
    fn raw(&self) -> *const c_void {
        self.0
    }
}

impl Drop for AVCaptureDeviceInput {
    fn drop(&mut self) {
        unsafe { super::sys::avrs_avf_release_object(self.0) }
    }
}
