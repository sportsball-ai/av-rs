use super::sys;
use std::{error::Error, fmt};

#[allow(clippy::missing_safety_doc)]
pub trait CFType {
    unsafe fn from_get_rule(cf: sys::CFTypeRef) -> Self;
    unsafe fn from_create_rule(cf: sys::CFTypeRef) -> Self;
    unsafe fn cf_type_ref(&self) -> sys::CFTypeRef;

    /// Gets the object's description if one is available.
    fn description(&self) -> crate::StringRef {
        unsafe { crate::StringRef::from_create_rule(sys::CFCopyDescription(self.cf_type_ref()) as _) }
    }
}

#[derive(Copy, Clone, Debug)]
pub struct OSStatus(sys::OSStatus);

impl OSStatus {
    pub fn as_sys(&self) -> sys::OSStatus {
        self.0
    }
}

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
