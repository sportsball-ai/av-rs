#![allow(
    deref_nullptr,
    non_snake_case,
    non_upper_case_globals,
    non_camel_case_types,
    clippy::unreadable_literal,
    clippy::cognitive_complexity
)]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

/// # Safety
/// This wraps the C library's `x264_encoder_open` function and comes with all the same safety
/// concerns.
pub unsafe fn x264_encoder_open(params: *mut x264_param_t) -> *mut x264_t {
    x264_encoder_open_wrapper(params)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_x264_param_default() {
        let mut params: std::mem::MaybeUninit<x264_param_t> = std::mem::MaybeUninit::uninit();
        unsafe {
            x264_param_default(params.as_mut_ptr());
        }
    }
}
