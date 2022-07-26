use snafu::Snafu;
use xcoder_logan_sys as sys;

#[derive(Debug, Snafu)]
pub enum XcoderInitError {
    #[snafu(display("error (code = {code})"))]
    Unknown { code: sys::ni_retcode_t },
}

#[cfg(target_os = "linux")]
pub fn init(should_match_rev: bool, timeout_seconds: u32) -> Result<(), XcoderInitError> {
    let code = unsafe { sys::ni_rsrc_init(if should_match_rev { 1 } else { 0 }, timeout_seconds as _) };
    if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
        return Err(XcoderEncoderError::Unknown { code });
    }
    Ok(())
}
