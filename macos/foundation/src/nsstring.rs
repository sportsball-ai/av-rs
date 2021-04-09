use std::ffi::{c_void, CStr};

mod sys {
    use std::ffi::c_void;

    extern "C" {
        pub fn avrs_nsstring_utf8_string(str: *const c_void) -> *const i8;
    }
}

pub struct NSString(*const c_void);

impl NSString {
    pub unsafe fn from_raw(ptr: *const c_void) -> Self {
        Self(ptr)
    }

    pub fn as_str(&self) -> &str {
        let s = unsafe { CStr::from_ptr(sys::avrs_nsstring_utf8_string(self.0)) };
        s.to_str().expect("UTF8String should always return valid utf-8")
    }
}

impl Drop for NSString {
    fn drop(&mut self) {
        unsafe { super::sys::avrs_foundation_release_object(self.0) }
    }
}
