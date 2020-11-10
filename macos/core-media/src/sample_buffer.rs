use super::{sys, BlockBuffer, FormatDescription};
use core_foundation::{result, CFType, OSStatus};

pub struct SampleBuffer(sys::CMSampleBufferRef);
core_foundation::trait_impls!(SampleBuffer);

impl SampleBuffer {
    pub fn new<T: FormatDescription>(
        block_buffer: &BlockBuffer,
        format_description: Option<&T>,
        num_samples: usize,
        sample_sizes: Option<&[usize]>,
    ) -> Result<Self, OSStatus> {
        let mut ret = std::ptr::null_mut();
        let sample_sizes = sample_sizes.map(|v| v.iter().map(|&ss| ss as u64).collect::<Vec<_>>());
        result(
            unsafe {
                sys::CMSampleBufferCreate(
                    std::ptr::null(),
                    block_buffer.cf_type_ref() as _,
                    1,
                    None,
                    std::ptr::null_mut(),
                    match format_description {
                        Some(v) => v.cf_type_ref() as _,
                        None => std::ptr::null(),
                    },
                    num_samples as _,
                    0,
                    std::ptr::null(),
                    sample_sizes.as_ref().map(|v| v.len()).unwrap_or(0) as _,
                    match sample_sizes {
                        Some(v) => v.as_ptr(),
                        None => std::ptr::null(),
                    },
                    &mut ret as _,
                )
            }
            .into(),
        )?;
        Ok(Self(ret))
    }
}
