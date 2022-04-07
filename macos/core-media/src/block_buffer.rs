use super::sys;
use core_foundation::{result, OSStatus};

pub struct BlockBuffer(sys::CMBlockBufferRef);
core_foundation::trait_impls!(BlockBuffer);

impl BlockBuffer {
    /// Creates a new block buffer that references the given memory block, without copying it.
    ///
    /// # Safety
    /// This is unsafe because the caller must ensure that the given block out-lives the BlockBuffer.
    pub unsafe fn with_memory_block(block: &[u8]) -> Result<Self, OSStatus> {
        let mut ret = std::ptr::null_mut();
        result(
            sys::CMBlockBufferCreateWithMemoryBlock(
                std::ptr::null(),
                block.as_ptr() as _,
                block.len() as _,
                sys::kCFAllocatorNull,
                std::ptr::null(),
                0,
                block.len() as _,
                0,
                &mut ret as _,
            )
            .into(),
        )?;
        Ok(Self(ret))
    }

    pub fn create_contiguous(&self, offset: usize, len: usize) -> Result<Self, OSStatus> {
        unsafe {
            let mut ret = std::ptr::null_mut();
            result(
                sys::CMBlockBufferCreateContiguous(
                    std::ptr::null(), // structureAllocator
                    self.0,
                    std::ptr::null(), // blockAllocator
                    std::ptr::null(), // customBlockSource
                    offset as _,
                    len as _,
                    0,
                    &mut ret as _,
                )
                .into(),
            )?;
            Ok(Self(ret))
        }
    }

    pub fn data(&self, offset: usize) -> Result<&[u8], OSStatus> {
        let mut ptr = std::ptr::null_mut();
        let mut len = 0;
        unsafe {
            result(sys::CMBlockBufferGetDataPointer(self.0, offset as _, &mut len, std::ptr::null_mut(), &mut ptr).into())?;
            Ok(std::slice::from_raw_parts(ptr as _, len as _))
        }
    }

    pub fn data_len(&self) -> usize {
        unsafe { sys::CMBlockBufferGetDataLength(self.0) as _ }
    }

    pub fn copy_data_bytes(&self, offset: usize, dest: &mut [u8]) -> Result<(), OSStatus> {
        unsafe { result(sys::CMBlockBufferCopyDataBytes(self.0, offset as _, dest.len() as _, dest.as_mut_ptr() as _).into()) }
    }
}
