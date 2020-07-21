#[cfg(target_os = "macos")]
pub mod sys {
    #![allow(non_snake_case, non_upper_case_globals, non_camel_case_types)]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

#[cfg(target_os = "macos")]
pub mod format_description;

#[cfg(target_os = "macos")]
pub use format_description::*;

#[cfg(target_os = "macos")]
pub mod sample_buffer;

#[cfg(target_os = "macos")]
pub use sample_buffer::*;

#[cfg(target_os = "macos")]
pub mod block_buffer;

#[cfg(target_os = "macos")]
pub use block_buffer::*;
