use super::*;

#[repr(transparent)]
pub struct Number(sys::CFNumberRef);
crate::trait_impls!(Number);

macro_rules! types {
    ( $($rust_ty:ty => $cf_ty:ident,)+ ) => {
        $(
            impl From<$rust_ty> for Number {
                fn from(n: $rust_ty) -> Self {
                    unsafe { Self(sys::CFNumberCreate(std::ptr::null(), sys::$cf_ty as _, &n as *const $rust_ty as _)) }
                }
            }

            impl std::convert::TryFrom<Number> for $rust_ty {
                type Error = Number;
                fn try_from(value: Number) -> Result<Self, Self::Error> {
                    let mut out = Self::default();
                    match unsafe { sys::CFNumberGetValue(value.0, sys::$cf_ty as _, &mut out as *mut $rust_ty as _) } {
                        0 => Err(value),
                        _ => Ok(out),
                    }
                }
            }
        )*
    }
}

types! {
    i8 => kCFNumberSInt8Type,
    i16 => kCFNumberSInt16Type,
    i32 => kCFNumberSInt32Type,
    i64 => kCFNumberSInt64Type,
    u8 => kCFNumberCharType,
    u16 => kCFNumberShortType,
    u32 => kCFNumberIntType,
    u64 => kCFNumberLongType,
    f32 => kCFNumberFloat32Type,
    f64 => kCFNumberFloat64Type,
}

#[cfg(test)]
mod test {
    use std::convert::TryFrom;

    use super::*;

    #[track_caller]
    fn roundtrip<T>(t: T)
    where
        T: Copy + TryFrom<Number> + PartialEq + std::fmt::Debug,
        Number: From<T>,
    {
        let n = Number::from(t);
        assert_eq!(T::try_from(n).map_err(|_| ()).unwrap(), t);
    }

    #[test]
    fn test_number() {
        roundtrip(1_i8);
        roundtrip(1_i16);
        roundtrip(1_i32);
        roundtrip(1_i64);
        roundtrip(1_u8);
        roundtrip(1_u16);
        roundtrip(1_u32);
        roundtrip(1_u64);
        roundtrip(1_f32);
        roundtrip(1_f64);
    }
}
