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
use core_foundation::OSStatus;
pub use video_encoder::*;

pub mod video_encoder_list;
pub use video_encoder_list::{Encoder, EncoderList};

pub mod decompression_session;
pub use decompression_session::*;

pub enum Error {
    OSStatus { context: &'static str, inner: OSStatus },
    InnerChannelDropped,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::OSStatus { context, inner } => {
                write!(f, "{}: {}", context, inner)
            }
            Self::InnerChannelDropped => {
                write!(f, "inner channel exceeded capacity and failed; channel must be polled more often")
            }
        }
    }
}

impl std::fmt::Debug for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(self, f)
    }
}

impl std::error::Error for Error {}

trait ResultExt<T> {
    fn context(self, msg: &'static str) -> Result<T, Error>;
}

impl<T> ResultExt<T> for Result<T, OSStatus> {
    fn context(self, context: &'static str) -> Result<T, Error> {
        self.map_err(|inner| Error::OSStatus { context, inner })
    }
}
