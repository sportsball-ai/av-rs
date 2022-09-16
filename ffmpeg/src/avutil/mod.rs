use std::{ffi::CStr, os::raw::c_int};

#[derive(Copy, Clone, PartialEq, Eq)]
#[repr(transparent)]
pub struct Error(pub c_int);

impl Error {
    /// Wraps a ffmpeg return value that is negative iff there was an error.
    #[inline]
    pub fn wrap(raw: c_int) -> Result<c_int, Self> {
        if raw < 0 {
            return Err(Self(raw));
        }
        Ok(raw)
    }
}

impl std::error::Error for Error {}

impl std::fmt::Debug for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "Error({} /* {} */)", self.0, self)
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        const ARRAYLEN: usize = 64;
        let mut buf = [0; ARRAYLEN];
        let s = unsafe {
            // Note av_strerror uses strlcpy, so it guarantees a trailing NUL byte.
            ffmpeg_sys::av_strerror(self.0, buf.as_mut_ptr(), ARRAYLEN);
            CStr::from_ptr(buf.as_ptr())
        };
        f.write_str(&s.to_string_lossy())
    }
}

#[cfg(test)]
mod tests {
    use std::ffi::CString;

    use super::*;

    #[test]
    fn test_error() {
        let eof_formatted = format!("{}", Error(ffmpeg_sys::AVERROR_EOF));
        assert!(eof_formatted.contains("End of file"), "eof formatted is: {}", eof_formatted);

        // Debug should have both the number and the string.
        let eof_debug = format!("{:?}", Error(ffmpeg_sys::AVERROR_EOF));
        assert!(
            eof_debug.contains(&format!("{}", ffmpeg_sys::AVERROR_EOF)) && eof_debug.contains("End of file"),
            "eof debug is: {}",
            &eof_debug
        );

        // Errors should be round trippable to a CString. (This will fail if they contain NUL
        // bytes.)
        CString::new(eof_formatted).unwrap();
    }
}
