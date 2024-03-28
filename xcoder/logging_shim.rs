// The contents of this file are manually included into both
// xcoder-quadra-sys and xcoder-logan-310-sys.
// xcoder-logan-259-sys lacks the needed callback infrastructure.

use std::{
    borrow::Cow,
    ffi::{c_char, c_int, CStr},
    panic,
    process::abort,
};

#[no_mangle]
extern "C" fn rust_netint_callback(level: c_int, message: *const c_char) {
    let r = panic::catch_unwind(|| {
        let buf = unsafe { CStr::from_ptr(message) }.to_string_lossy();
        match level {
            crate::ni_log_level_t_NI_LOG_NONE => {}
            crate::ni_log_level_t_NI_LOG_FATAL | crate::ni_log_level_t_NI_LOG_ERROR => {
                log::error!("{buf}");
            }
            crate::ni_log_level_t_NI_LOG_INFO => {
                log::info!("{buf}");
            }
            crate::ni_log_level_t_NI_LOG_DEBUG => {
                log::debug!("{buf}");
            }
            crate::ni_log_level_t_NI_LOG_TRACE => {
                log::trace!("{buf}");
            }
            other => {
                log::error!("netint log level {other} unrecognized, message was {buf}");
            }
        }
    });
    if let Err(e) = r {
        // Strings and &str are the most common panic types and can be dropped themselves without panicking.
        // However if this isn't a string we don't have any reasonable way to assert what the type is.
        // Therefore that type could panic when dropped. This is not acceptable in a C abi function, so we'll
        // abort instead.
        let cast_to_cow = e
            .downcast::<String>()
            .map(|b| *b)
            .map(Cow::Owned)
            .or_else(|e| e.downcast::<&'static str>().map(|b| *b).map(Cow::Borrowed));
        if cast_to_cow.is_err() {
            abort();
        }
    }
}
