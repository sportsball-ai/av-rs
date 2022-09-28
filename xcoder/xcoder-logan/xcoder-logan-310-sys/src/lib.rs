#![allow(
    dead_code,
    deref_nullptr,
    non_upper_case_globals,
    non_snake_case,
    non_camel_case_types,
    unaligned_references,
    clippy::redundant_static_lifetimes,
    clippy::too_many_arguments,
    clippy::useless_transmute
)]

#[cfg(target_os = "linux")]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

#[cfg(all(test, target_os = "linux"))]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        unsafe {
            ni_logan_rsrc_print_all_devices_capability();
        }
    }
}
