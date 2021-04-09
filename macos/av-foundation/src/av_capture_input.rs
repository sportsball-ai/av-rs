use std::ffi::c_void;

pub trait AVCaptureInput {
    fn raw(&self) -> *const c_void;
}
