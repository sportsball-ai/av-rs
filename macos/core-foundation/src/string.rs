use std::ffi::CStr;

use super::*;

#[repr(transparent)]
pub struct StringRef(sys::CFStringRef);

impl StringRef {
    #[inline]
    pub fn from_static(value: &'static str) -> Self {
        Self(unsafe {
            sys::CFStringCreateWithBytesNoCopy(
                std::ptr::null(),
                value.as_ptr(),
                value.len() as _,
                sys::kCFStringEncodingUTF8,
                0,
                sys::kCFAllocatorNull,
            )
        })
    }

    fn as_str(&self) -> Option<&str> {
        unsafe {
            let ptr = sys::CFStringGetCStringPtr(self.0, sys::kCFStringEncodingUTF8);
            if ptr.is_null() {
                return None;
            }
            Some(CStr::from_ptr(ptr).to_str().expect("kCFStringEncodingUTF8 should produce valid UTF-8"))
        }
    }

    fn to_std(&self) -> String {
        unsafe {
            let full_range = sys::CFRange {
                location: 0,
                length: sys::CFStringGetLength(self.0), // in characters, not bytes.
            };
            let mut byte_len = 0;
            sys::CFStringGetBytes(
                self.0,
                full_range,
                sys::kCFStringEncodingUTF8,
                0,
                false as _,
                std::ptr::null_mut(),
                0,
                &mut byte_len,
            );
            let mut out = vec![0; byte_len as usize];
            sys::CFStringGetBytes(
                self.0,
                full_range,
                sys::kCFStringEncodingUTF8,
                0,
                false as _,
                out.as_mut_ptr(),
                byte_len,
                std::ptr::null_mut(),
            );
            std::string::String::from_utf8(out).expect("kCFStringEncodingUTF8 should produce valid UTF-8")
        }
    }
}

trait_impls!(StringRef);

impl std::fmt::Display for StringRef {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.as_str() {
            Some(borrowed) => borrowed.fmt(f),
            None => self.to_std().fmt(f),
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn create() {
        let s = super::StringRef::from_static("asdf");
        assert_eq!(s.to_string(), "asdf");
    }
}
