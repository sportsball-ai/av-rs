use super::*;
use crate as core_foundation;

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
