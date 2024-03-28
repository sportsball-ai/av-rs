// The contents of this file are manually included into both
// xcoder-quadra-sys and xcoder-logan-310-sys.
// xcoder-logan-259-sys lacks the needed callback infrastructure.

use std::{
    borrow::Cow,
    ffi::{c_char, c_int, CStr},
    panic,
    process::abort,
};

extern "C" {
    pub fn setup_rust_netint_logging();
}

#[no_mangle]
#[allow(clippy::useless_conversion)]
extern "C" fn rust_netint_callback(level: c_int, message: *const c_char) {
    let r = panic::catch_unwind(|| {
        let buf = unsafe { CStr::from_ptr(message) }.to_string_lossy();
        // The log constants might be either u32 or i32. So we'll author this code in a polyglot
        // manner. That means we can't use pattern matching. bindgen does not currently expose
        // a way for us to state a preferred type here.
        //
        // Order this from most verbose to least verbose, operating on the belief that more verbose
        // levels are more common.
        if crate::ni_log_level_t_NI_LOG_TRACE == level as _ {
            log::trace!("{buf}");
        } else if crate::ni_log_level_t_NI_LOG_DEBUG == level as _ {
            log::debug!("{buf}");
        } else if crate::ni_log_level_t_NI_LOG_INFO == level as _ {
            log::info!("{buf}");
        } else if [crate::ni_log_level_t_NI_LOG_FATAL, ni_log_level_t_NI_LOG_ERROR].contains(&(level as _)) {
            log::error!("{buf}");
        } else if crate::ni_log_level_t_NI_LOG_NONE == level as _ {
            // Do nothing
        } else {
            log::error!("netint log level {level} unrecognized, message was {buf}");
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
