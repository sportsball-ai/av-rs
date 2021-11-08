#![cfg(target_os = "macos")]

pub mod sys {
    #![allow(
        deref_nullptr,
        non_snake_case,
        non_upper_case_globals,
        non_camel_case_types,
        unaligned_references,
        clippy::unreadable_literal,
        clippy::cognitive_complexity
    )]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub mod audio_queue;
pub use audio_queue::*;
