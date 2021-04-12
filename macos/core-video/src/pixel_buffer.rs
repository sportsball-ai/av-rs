use super::sys;
use std::ffi::c_void;

pub struct PixelBuffer(sys::CVPixelBufferRef);
core_foundation::trait_impls!(PixelBuffer);

impl PixelBuffer {
    pub fn pixel_format_type(&self) -> u32 {
        unsafe { sys::CVPixelBufferGetPixelFormatType(self.0) }
    }

    pub fn width(&self) -> u64 {
        unsafe { sys::CVPixelBufferGetWidth(self.0) }
    }

    pub fn height(&self) -> u64 {
        unsafe { sys::CVPixelBufferGetHeight(self.0) }
    }

    pub fn base_address_of_plane(&self, i: usize) -> *const c_void {
        unsafe { sys::CVPixelBufferGetBaseAddressOfPlane(self.0, i as _) }
    }

    pub fn base_address(&self) -> *const c_void {
        unsafe { sys::CVPixelBufferGetBaseAddress(self.0) }
    }

    pub fn lock_base_address(&self) {
        unsafe { sys::CVPixelBufferLockBaseAddress(self.0, sys::kCVPixelBufferLock_ReadOnly as _) };
    }

    pub fn unlock_base_address(&self) {
        unsafe { sys::CVPixelBufferUnlockBaseAddress(self.0, sys::kCVPixelBufferLock_ReadOnly as _) };
    }

    pub fn data_size(&self) -> usize {
        unsafe { sys::CVPixelBufferGetDataSize(self.0) as usize }
    }
}
