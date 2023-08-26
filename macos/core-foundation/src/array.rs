use super::*;

pub struct Array(sys::CFArrayRef);
crate::trait_impls!(Array);

impl Array {
    pub fn len(&self) -> usize {
        unsafe { sys::CFArrayGetCount(self.0) as _ }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// # Safety
    /// Behavior is undefined if the value is not of type `T` or if the index is out of range.
    pub unsafe fn cf_type_value_at_index<T: CFType>(&self, idx: usize) -> Option<T> {
        let v = sys::CFArrayGetValueAtIndex(self.0, idx as _);
        if v.is_null() {
            None
        } else {
            Some(T::with_cf_type_ref(v as _))
        }
    }
}

impl<T: CFType> From<&[T]> for Array {
    fn from(value: &[T]) -> Self {
        Self(unsafe { sys::CFArrayCreate(std::ptr::null(), value.as_ptr() as _, value.len() as _, &sys::kCFTypeArrayCallBacks) })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn create() {
        let _ = Array::from(&[Number::from(1), Number::from(2), Number::from(3)][..]);
    }
}
