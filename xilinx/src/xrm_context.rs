use crate::sys;

/// A wrapper of the XrmContext which will correctly drop the context when dropped
pub struct XrmContext {
    context: sys::xrmContext,
}

impl XrmContext {
    pub fn new() -> Self {
        Self::default()
    }

    /// SAFETY: If the `XrmContext` wrapping structure is dropped this raw value may
    /// be freed. Do not use unless the source `XrmContext` still exists.
    pub unsafe fn raw(&self) -> sys::xrmContext {
        self.context
    }
}

impl Default for XrmContext {
    fn default() -> Self {
        let context = unsafe { sys::xrmCreateContext(sys::XRM_API_VERSION_1) };
        Self { context }
    }
}

impl Drop for XrmContext {
    fn drop(&mut self) {
        unsafe {
            unsafe {
                sys::xrmDestroyContext(self.context);
            }
        }
    }
}
