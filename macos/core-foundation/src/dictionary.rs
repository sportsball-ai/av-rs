use super::*;
use std::os::raw::c_void;

pub struct Dictionary(sys::CFDictionaryRef);
crate::trait_impls!(Dictionary);

impl Dictionary {
    /// # Safety
    /// Behavior is undefined if the value is not of type `T`.
    pub unsafe fn cf_type_value<T: CFType>(&self, key: *const c_void) -> Option<T> {
        let mut v = std::ptr::null();
        if sys::CFDictionaryGetValueIfPresent(self.0, key, &mut v as *mut _ as _) != 0 {
            Some(T::with_cf_type_ref(v as _))
        } else {
            None
        }
    }
}

pub struct MutableDictionary(sys::CFMutableDictionaryRef);
crate::trait_impls!(MutableDictionary);

impl MutableDictionary {
    /// Creates a new mutable dictionary which will only contain CFType objects.
    pub fn new_cf_type() -> Self {
        Self::cf_type_with_capacity(0)
    }

    /// Creates a new mutable dictionary which will only contain CFType objects and has the given capacity.
    pub fn cf_type_with_capacity(capacity: usize) -> Self {
        unsafe {
            Self(sys::CFDictionaryCreateMutable(
                std::ptr::null_mut(),
                capacity as _,
                &sys::kCFTypeDictionaryKeyCallBacks as _,
                &sys::kCFTypeDictionaryValueCallBacks as _,
            ))
        }
    }

    /// # Safety
    /// Behavior is undefined if the key and value are not of the expected type.
    pub unsafe fn set_value(&mut self, key: *const c_void, value: *const c_void) {
        sys::CFDictionarySetValue(self.0, key, value);
    }
}

impl From<MutableDictionary> for Dictionary {
    fn from(desc: MutableDictionary) -> Self {
        unsafe { Self::with_cf_type_ref(desc.0 as _) }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_dictionary() {
        let mut d = MutableDictionary::new_cf_type();

        unsafe {
            let key = sys::kCFStringTransformToLatin;
            d.set_value(key as _, Boolean::from(true).cf_type_ref() as _);
            let d = Dictionary::from(d);
            assert!(d.cf_type_value::<Boolean>(key as _).unwrap().value());
        }
    }
}
