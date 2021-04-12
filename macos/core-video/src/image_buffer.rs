use super::{sys, PixelBuffer};
use core_foundation::CFType;

pub struct ImageBuffer(sys::CVImageBufferRef);
core_foundation::trait_impls!(ImageBuffer);

impl ImageBuffer {
    pub fn pixel_buffer(&self) -> PixelBuffer {
        unsafe { PixelBuffer::with_cf_type_ref(self.0 as _) }
    }
}
