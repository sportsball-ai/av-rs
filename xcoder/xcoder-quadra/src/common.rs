use snafu::Snafu;
use xcoder_logan_sys as sys;

#[derive(Debug, Snafu)]
pub enum XcoderInitError {
    #[snafu(display("error {operation} (code = {code})"))]
    Unknown { code: sys::ni_retcode_t },
}

#[cfg(target_os = "linux")]
pub fn init(should_match_rev: bool, timeout_seconds: u32) -> Result<(), XcoderInitError> {
    let code = unsafe { sys::ni_rsrc_init(should_match_rev, timeout_seconds) };
    if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
        return Err(XcoderEncoderError::Unknown { code });
    }
    Ok(())
}
