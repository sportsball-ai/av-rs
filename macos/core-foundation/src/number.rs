use super::*;

#[repr(transparent)]
pub struct Number(sys::CFNumberRef);
crate::trait_impls!(Number);

impl From<i8> for Number {
    fn from(n: i8) -> Self {
        unsafe { Self(sys::CFNumberCreate(std::ptr::null(), sys::kCFNumberSInt8Type as _, &n as *const i8 as _)) }
    }
}

impl From<i16> for Number {
    fn from(n: i16) -> Self {
        unsafe { Self(sys::CFNumberCreate(std::ptr::null(), sys::kCFNumberSInt16Type as _, &n as *const i16 as _)) }
    }
}

impl From<i32> for Number {
    fn from(n: i32) -> Self {
        unsafe { Self(sys::CFNumberCreate(std::ptr::null(), sys::kCFNumberSInt32Type as _, &n as *const i32 as _)) }
    }
}

impl From<i64> for Number {
    fn from(n: i64) -> Self {
        unsafe { Self(sys::CFNumberCreate(std::ptr::null(), sys::kCFNumberSInt64Type as _, &n as *const i64 as _)) }
    }
}

impl From<f32> for Number {
    fn from(n: f32) -> Self {
        unsafe { Self(sys::CFNumberCreate(std::ptr::null(), sys::kCFNumberFloat32Type as _, &n as *const f32 as _)) }
    }
}

impl From<f64> for Number {
    fn from(n: f64) -> Self {
        unsafe { Self(sys::CFNumberCreate(std::ptr::null(), sys::kCFNumberFloat64Type as _, &n as *const f64 as _)) }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_number() {
        let _ = Number::from(1_i8);
        let _ = Number::from(1_i16);
        let _ = Number::from(1_i32);
        let _ = Number::from(1_i64);
        let _ = Number::from(1_f32);
        let _ = Number::from(1_f64);
    }
}
