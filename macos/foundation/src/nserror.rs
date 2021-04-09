use super::NSString;
use std::{error::Error, ffi::c_void, fmt};

mod sys {
    use std::ffi::c_void;

    extern "C" {
        pub fn avrs_nserror_localized_description(err: *const c_void) -> *const c_void;
    }
}

pub struct NSError(*const c_void);

unsafe impl Send for NSError {}
unsafe impl Sync for NSError {}

impl fmt::Debug for NSError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.localized_description())
    }
}

impl fmt::Display for NSError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.localized_description())
    }
}

impl Error for NSError {}

impl NSError {
    pub unsafe fn from_raw(ptr: *const c_void) -> Self {
        Self(ptr)
    }

    pub fn localized_description(&self) -> String {
        let s = unsafe { NSString::from_raw(sys::avrs_nserror_localized_description(self.0)) };
        s.as_str().to_string()
    }
}

impl Drop for NSError {
    fn drop(&mut self) {
        unsafe { super::sys::avrs_foundation_release_object(self.0) }
    }
}
