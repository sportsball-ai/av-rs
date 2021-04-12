use std::ffi::c_void;

pub trait AVCaptureOutput {
    fn raw(&self) -> *const c_void;
}
