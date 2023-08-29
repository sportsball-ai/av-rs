use super::{sys, ImageBuffer};
use core_foundation::{result, CFType, OSStatus};
use std::ffi::c_void;

pub struct PixelBuffer(sys::CVPixelBufferRef);
core_foundation::trait_impls!(PixelBuffer);

pub struct PixelBufferPlane {
    pub height: u32,
    pub width: u32,
    pub bytes_per_row: usize,
    pub data: *mut u8,
}

impl PixelBuffer {
    /// Creates a new pixel buffer that references the bytes, without copying them.
    ///
    /// # Safety
    /// This is unsafe because the caller must ensure that the given bytes out-live the PixelBuffer.
    pub unsafe fn with_bytes(width: u32, height: u32, pixel_format_type: u32, bytes_per_pixel: u32, data_ptr: *mut u8) -> Result<Self, OSStatus> {
        let mut ret = std::ptr::null_mut();
        result(
            sys::CVPixelBufferCreateWithBytes(
                std::ptr::null(), // allocator
                width as _,
                height as _,
                pixel_format_type,
                data_ptr as _,                      // dataPtr
                (width * bytes_per_pixel) as usize, // dataSize
                None,                               // releaseCallback
                std::ptr::null_mut(),               // releaseRefCon
                std::ptr::null_mut(),               // pixelBufferAttributes
                &mut ret as _,
            )
            .into(),
        )?;
        Ok(Self(ret))
    }

    /// Creates a new pixel buffer that references the given planes, without copying them.
    ///
    /// # Safety
    /// This is unsafe because the caller must ensure that the given planes out-live the PixelBuffer.
    pub unsafe fn with_planar_bytes<P>(width: u32, height: u32, pixel_format_type: u32, planes: P) -> Result<Self, OSStatus>
    where
        P: IntoIterator<Item = PixelBufferPlane>,
    {
        let mut plane_ptrs = vec![];
        let mut plane_widths = vec![];
        let mut plane_heights = vec![];
        let mut plane_bytes_per_row = vec![];
        for p in planes.into_iter() {
            plane_ptrs.push(p.data as *mut c_void);
            plane_widths.push(p.width as usize);
            plane_heights.push(p.height as usize);
            plane_bytes_per_row.push(p.bytes_per_row);
        }

        let mut ret = std::ptr::null_mut();
        result(
            sys::CVPixelBufferCreateWithPlanarBytes(
                std::ptr::null(), // allocator
                width as _,
                height as _,
                pixel_format_type,
                std::ptr::null_mut(), // dataPtr
                0,                    // dataSize
                plane_widths.len() as _,
                plane_ptrs.as_mut_ptr(),
                plane_widths.as_mut_ptr(),
                plane_heights.as_mut_ptr(),
                plane_bytes_per_row.as_mut_ptr(),
                None,                 // releaseCallback
                std::ptr::null_mut(), // releaseRefCon
                std::ptr::null_mut(), // pixelBufferAttributes
                &mut ret as _,
            )
            .into(),
        )?;
        Ok(Self(ret))
    }

    pub fn pixel_format_type(&self) -> u32 {
        unsafe { sys::CVPixelBufferGetPixelFormatType(self.0) }
    }

    pub fn width(&self) -> usize {
        unsafe { sys::CVPixelBufferGetWidth(self.0) }
    }

    pub fn height(&self) -> usize {
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
        unsafe { sys::CVPixelBufferGetDataSize(self.0) }
    }
}

impl From<PixelBuffer> for ImageBuffer {
    fn from(b: PixelBuffer) -> Self {
        unsafe { ImageBuffer::from_get_rule(b.0 as _) }
    }
}
