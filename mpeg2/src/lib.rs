#![cfg_attr(not(any(feature = "std", test)), no_std)]

#[macro_use]
extern crate alloc;

pub use core2::io;

use core::fmt;

#[derive(Debug)]
pub struct DecodeError {
    message: &'static str,
}

impl DecodeError {
    pub fn new(message: &'static str) -> Self {
        Self { message }
    }
}

impl fmt::Display for DecodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}

#[cfg(feature = "std")]
impl std::error::Error for DecodeError {}

#[derive(Debug)]
pub enum EncodeError {
    Io(io::Error),
    Other(&'static str),
}

impl EncodeError {
    pub fn other(message: &'static str) -> Self {
        Self::Other(message)
    }
}

impl From<io::Error> for EncodeError {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

impl fmt::Display for EncodeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(e) => write!(f, "io error: {}", e),
            Self::Other(msg) => write!(f, "{}", msg),
        }
    }
}

#[cfg(feature = "std")]
impl std::error::Error for EncodeError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(e) => Some(e),
            Self::Other(_) => None,
        }
    }
}

pub mod bitstream;
pub mod muxer;
pub mod pes;
pub mod temi;
pub mod ts;
