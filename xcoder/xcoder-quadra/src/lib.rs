#[cfg(target_os = "linux")]
mod common;

#[cfg(target_os = "linux")]
mod encoder;

#[cfg(target_os = "linux")]
pub use common::*;

#[cfg(target_os = "linux")]
pub use encoder::*;
