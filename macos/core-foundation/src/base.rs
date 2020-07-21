use super::sys;
use std::{error::Error, ffi::CStr, fmt};

pub trait CFType {
    unsafe fn with_cf_type_ref(cf: sys::CFTypeRef) -> Self;
    unsafe fn cf_type_ref(&self) -> sys::CFTypeRef;

    /// Gets the object's description if one is available.
    fn description(&self) -> Option<String> {
        unsafe {
            let desc = sys::CFCopyDescription(self.cf_type_ref());
            let ptr = sys::CFStringGetCStringPtr(desc, sys::kCFStringEncodingUTF8);
            let ret = CStr::from_ptr(ptr).to_str().ok().map(|s| s.to_string());
            sys::CFRelease(desc as _);
            ret
        }
    }
}

#[derive(Copy, Clone, Debug)]
pub struct OSStatus(sys::OSStatus);

impl fmt::Display for OSStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "os status {}", self.0)
    }
}

impl Error for OSStatus {}

impl From<sys::OSStatus> for OSStatus {
    fn from(status: sys::OSStatus) -> Self {
        Self(status)
    }
}

pub fn result(status: OSStatus) -> Result<(), OSStatus> {
    match status.0 {
        0 => Ok(()),
        _ => Err(status),
    }
}
