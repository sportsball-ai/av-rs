use super::{AVCaptureInput, AVCaptureOutput};
use std::ffi::c_void;

pub mod sys {
    use std::ffi::c_void;

    extern "C" {
        pub fn avrs_new_av_capture_session() -> *const c_void;
        pub fn avrs_av_capture_session_can_add_input(sess: *const c_void, input: *const c_void) -> bool;
        pub fn avrs_av_capture_session_add_input(sess: *const c_void, input: *const c_void);
        pub fn avrs_av_capture_session_can_add_output(sess: *const c_void, output: *const c_void) -> bool;
        pub fn avrs_av_capture_session_add_output(sess: *const c_void, output: *const c_void);
        pub fn avrs_av_capture_session_start_running(sess: *const c_void);
        pub fn avrs_av_capture_session_stop_running(sess: *const c_void);
        pub fn avrs_av_capture_session_begin_configuration(sess: *const c_void);
        pub fn avrs_av_capture_session_commit_configuration(sess: *const c_void);
    }
}

pub struct AVCaptureSession(*const c_void);

impl AVCaptureSession {
    pub fn new() -> Self {
        Self(unsafe { sys::avrs_new_av_capture_session() })
    }

    pub fn begin_configuration(&self) {
        unsafe { sys::avrs_av_capture_session_begin_configuration(self.0) }
    }

    pub fn commit_configuration(&self) {
        unsafe { sys::avrs_av_capture_session_commit_configuration(self.0) }
    }

    pub fn can_add_input<I: AVCaptureInput>(&self, input: I) -> bool {
        unsafe { sys::avrs_av_capture_session_can_add_input(self.0, input.raw()) }
    }

    pub fn add_input<I: AVCaptureInput>(&self, input: I) {
        unsafe { sys::avrs_av_capture_session_add_input(self.0, input.raw()) }
    }

    pub fn can_add_output<O: AVCaptureOutput>(&self, output: O) -> bool {
        unsafe { sys::avrs_av_capture_session_can_add_output(self.0, output.raw()) }
    }

    pub fn add_output<O: AVCaptureOutput>(&self, output: O) {
        unsafe { sys::avrs_av_capture_session_add_output(self.0, output.raw()) }
    }

    pub fn start_running(&self) {
        unsafe { sys::avrs_av_capture_session_start_running(self.0) }
    }

    pub fn stop_running(&self) {
        unsafe { sys::avrs_av_capture_session_stop_running(self.0) }
    }
}

unsafe impl Send for AVCaptureSession {}

impl Drop for AVCaptureSession {
    fn drop(&mut self) {
        unsafe { super::sys::avrs_avf_release_object(self.0) }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_av_capture_session() {
        let _ = AVCaptureSession::new();
    }
}
