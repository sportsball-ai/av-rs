#![cfg(target_os = "macos")]

pub mod sys {
    use std::ffi::c_void;

    extern "C" {
        pub fn avrs_foundation_release_object(obj: *const c_void);
    }
}

pub mod nserror;
pub use nserror::*;

pub mod nsstring;
pub use nsstring::*;
