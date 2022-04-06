use super::*;

pub struct Boolean(sys::CFBooleanRef);
crate::trait_impls!(Boolean);

impl Boolean {
    pub fn value(&self) -> bool {
        unsafe { sys::CFBooleanGetValue(self.0) != 0 }
    }
}

impl From<bool> for Boolean {
    fn from(b: bool) -> Self {
        unsafe {
            Self::with_cf_type_ref(match b {
                true => sys::kCFBooleanTrue,
                false => sys::kCFBooleanFalse,
            } as _)
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_boolean() {
        let b = Boolean::from(true);
        assert!(b.value());

        let b = Boolean::from(false);
        assert!(!b.value());
    }
}
