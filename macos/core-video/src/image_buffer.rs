use super::sys;

pub struct ImageBuffer(sys::CVImageBufferRef);
core_foundation::trait_impls!(ImageBuffer);
