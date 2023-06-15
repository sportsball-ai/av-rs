#![allow(
    dead_code,
    deref_nullptr,
    non_upper_case_globals,
    non_snake_case,
    non_camel_case_types,
    clippy::redundant_static_lifetimes,
    clippy::too_many_arguments,
    clippy::useless_transmute
)]

#[cfg(target_os = "linux")]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
