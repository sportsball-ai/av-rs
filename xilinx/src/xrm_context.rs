use crate::{sys, xlnx_fill_dec_pool_props, xlnx_fill_enc_pool_props, xlnx_fill_scal_pool_props, xrmCheckCuPoolAvailableNumV2, xrmCuPoolPropertyV2, Error};

/// A wrapper of the XrmContext which will correctly drop the context when dropped.
pub struct XrmContext {
    context: sys::xrmContext,
}

unsafe impl Send for XrmContext {}
// Sync is omitted on purpose as I have no reason to believe these APIs are thread safe.

impl XrmContext {
    pub fn new() -> Self {
        Self::default()
    }

    /// SAFETY: If the `XrmContext` wrapping structure is dropped this raw value may
    /// be freed. Do not use unless the source `XrmContext` still exists.
    pub unsafe fn raw(&self) -> sys::xrmContext {
        self.context
    }

    /// An unloaded U30 card has 2000 scaling units available. This should be used exclusively for monitoring scaling resources, not for reservations. It shouldn't even
    /// be used to determine if you can make a reservation.
    pub fn scal_cu_available(&self) -> Result<i32, Error> {
        let mut cu_prop: Box<xrmCuPoolPropertyV2> = Box::new(Default::default());
        // Use a load of 1 to get the most fine grained result
        xlnx_fill_scal_pool_props(cu_prop.as_mut(), 1, None)?;
        let num_cu = unsafe { xrmCheckCuPoolAvailableNumV2(self.raw(), cu_prop.as_mut()) };
        Ok(num_cu)
    }

    /// An unloaded U30 card has 64 decoding units available. This should be used exclusively for monitoring decoding resources, not for reservations. It shouldn't even
    /// be used to determine if you can make a reservation.
    pub fn dec_cu_available(&self) -> Result<i32, Error> {
        let mut cu_pool_prop: Box<xrmCuPoolPropertyV2> = Box::new(Default::default());
        // Use a load of 1 to get the most fine grained result
        xlnx_fill_dec_pool_props(&mut cu_pool_prop, 1, None)?;
        let num_cu_pool = unsafe { xrmCheckCuPoolAvailableNumV2(self.raw(), cu_pool_prop.as_mut()) };
        Ok(num_cu_pool)
    }

    /// An unloaded U30 card has 64 encoding units available. This should be used exclusively for monitoring encoding resources, not for reservations. It shouldn't even
    /// be used to determine if you can make a reservation.
    pub fn enc_cu_available(&self) -> Result<i32, Error> {
        let mut cu_pool_prop: Box<xrmCuPoolPropertyV2> = Box::new(Default::default());
        // Use a load of 1 to get the most fine grained result
        xlnx_fill_enc_pool_props(&mut cu_pool_prop, 1, 1, None)?;
        let num_cu_pool = unsafe { xrmCheckCuPoolAvailableNumV2(self.raw(), cu_pool_prop.as_mut()) };
        Ok(num_cu_pool)
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
            sys::xrmDestroyContext(self.context);
        }
    }
}
