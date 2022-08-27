#![allow(
    deref_nullptr,
    non_snake_case,
    non_upper_case_globals,
    non_camel_case_types,
    clippy::unreadable_literal,
    clippy::cognitive_complexity
)]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

pub use libc::{c_char as char, c_int as int, c_void as void, sockaddr_storage, socklen_t};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        unsafe {
            assert_eq!(srt_startup(), 0);
            assert_eq!(srt_cleanup(), 0);
        }
    }
}
