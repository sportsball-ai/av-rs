#![cfg(target_os = "macos")]

pub mod sys {
    use std::ffi::c_void;

    extern "C" {
        pub fn avrs_avf_release_object(obj: *const c_void);
    }
}

pub mod av_capture_device;
pub use av_capture_device::*;

pub mod av_capture_input;
pub use av_capture_input::*;

pub mod av_capture_output;
pub use av_capture_output::*;

pub mod av_capture_video_data_output;
pub use av_capture_video_data_output::*;

pub mod av_capture_session;
pub use av_capture_session::*;

pub mod av_media_type;
pub use av_media_type::*;
