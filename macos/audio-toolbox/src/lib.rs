#![cfg(target_os = "macos")]

pub mod sys {
    #![allow(
        non_snake_case,
        non_upper_case_globals,
        non_camel_case_types,
        clippy::unreadable_literal,
        clippy::cognitive_complexity
    )]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub mod audio_queue;
pub use audio_queue::*;
