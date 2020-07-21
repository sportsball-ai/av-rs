#[cfg(target_os = "macos")]
pub mod sys {
    #![allow(non_snake_case, non_upper_case_globals, non_camel_case_types)]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

#[cfg(target_os = "macos")]
pub mod image_buffer;

#[cfg(target_os = "macos")]
pub use image_buffer::*;
