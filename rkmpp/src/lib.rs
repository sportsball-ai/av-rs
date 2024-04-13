mod encoder;
pub use encoder::*;

/// This module abstracts away the low-level bindings to the Rockchip MPP library with slightly
/// safer and higher-level abstractions to take care of things like symbol loading and resource
/// management.
pub mod mpp;
