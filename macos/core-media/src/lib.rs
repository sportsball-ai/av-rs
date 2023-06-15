#![cfg(target_os = "macos")]

pub mod sys {
    #![allow(
        deref_nullptr,
        non_snake_case,
        non_upper_case_globals,
        non_camel_case_types,
        clippy::unreadable_literal,
        clippy::cognitive_complexity
    )]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub mod video_codec_type;
pub use video_codec_type::*;

pub mod time;
pub use time::*;

pub mod format_description;
pub use format_description::*;

pub mod sample_buffer;
pub use sample_buffer::*;

pub mod block_buffer;
pub use block_buffer::*;
