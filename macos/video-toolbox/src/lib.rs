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

pub mod compression_session;
pub use compression_session::*;

pub mod video_encoder;
pub use video_encoder::*;

pub mod decompression_session;
pub use decompression_session::*;
