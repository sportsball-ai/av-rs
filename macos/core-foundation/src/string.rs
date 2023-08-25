use super::*;

#[repr(transparent)]
pub struct StringRef(sys::CFStringRef);

impl StringRef {
    pub fn from_static(value: &'static str) -> Self {
        Self(unsafe {
            sys::CFStringCreateWithBytesNoCopy(
                std::ptr::null_mut(),
                value.as_ptr(),
                value.len() as _,
                sys::kCFStringEncodingUTF8,
                0,
                sys::kCFAllocatorNull,
            )
        })
    }
}

trait_impls!(StringRef);

#[cfg(test)]
mod tests {
    #[test]
    fn create() {
        let _ = super::StringRef::from_static("asdf");
    }
}
