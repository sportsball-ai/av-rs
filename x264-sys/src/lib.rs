#![allow(
    deref_nullptr,
    non_snake_case,
    non_upper_case_globals,
    non_camel_case_types,
    clippy::unreadable_literal,
    clippy::cognitive_complexity
)]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

pub unsafe fn x264_encoder_open(params: *mut x264_param_t) -> *mut x264_t {
    x264_encoder_open_wrapper(params)
}
