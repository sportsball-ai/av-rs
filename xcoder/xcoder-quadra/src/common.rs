use snafu::Snafu;
use xcoder_quadra_sys as sys;

#[derive(Debug, Snafu)]
pub enum XcoderInitError {
    #[snafu(display("error (code = {code})"))]
    Unknown { code: sys::ni_retcode_t },
}

#[derive(Debug, Clone)]
pub struct XcoderHardware {
    pub id: i32,
    pub device_handle: i32,
}

#[cfg(target_os = "linux")]
pub fn init(should_match_rev: bool, timeout_seconds: u32) -> Result<(), XcoderInitError> {
    let code = unsafe { sys::ni_rsrc_init(if should_match_rev { 1 } else { 0 }, timeout_seconds as _) };
    if code != sys::ni_retcode_t_NI_RETCODE_SUCCESS {
        return Err(XcoderInitError::Unknown { code });
    }
    Ok(())
}

pub(crate) fn fps_to_rational(fps: f64) -> (i32, i32) {
    let den = if fps.fract() == 0.0 {
        1000
    } else {
        // a denominator of 1001 for 29.97 or 59.94 is more
        // conventional
        1001
    };
    let num = (fps * den as f64).round() as _;
    (den, num)
}
