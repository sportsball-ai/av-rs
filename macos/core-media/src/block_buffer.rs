use super::sys;
use core_foundation::{result, OSStatus};

pub struct BlockBuffer(sys::CMBlockBufferRef);
core_foundation::trait_impls!(BlockBuffer);

impl BlockBuffer {
    /// Creates a new block buffer that references the given memory block, without copying it. This
    /// is unsafe because the caller must ensure that the given block out-lives the BlockBuffer.
    #[allow(clippy::missing_safety_doc)]
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
}
