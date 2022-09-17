#[macro_use]
extern crate thiserror;

pub mod avformat;
pub mod avutil;

pub use ffmpeg_sys as sys;

pub use avutil::Error;
