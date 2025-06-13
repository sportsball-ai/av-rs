#![allow(
    dead_code,
    deref_nullptr,
    non_upper_case_globals,
    non_snake_case,
    non_camel_case_types,
    improper_ctypes,
    clippy::redundant_static_lifetimes,
    clippy::too_many_arguments
)]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

unsafe impl Send for XmaParameter { }