#![cfg(target_os = "macos")]

pub mod sys {
    #![allow(
        deref_nullptr,
        non_snake_case,
        non_upper_case_globals,
        non_camel_case_types,
        clippy::unreadable_literal,
        clippy::cognitive_complexity
    )]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub mod base;
pub use base::*;

pub mod array;
pub use array::*;

pub mod boolean;
pub use boolean::*;

pub mod dictionary;
pub use dictionary::*;

pub mod number;
pub use number::*;

pub mod string;
pub use string::*;

#[macro_export]
macro_rules! trait_impls {
    ($e:ident) => {
        impl std::fmt::Debug for $e {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                use $crate::CFType;
                write!(f, "{}", self.description())
            }
        }

        impl Drop for $e {
            fn drop(&mut self) {
                unsafe { $crate::sys::CFRelease(self.0 as _) }
            }
        }

        unsafe impl Send for $e {}

        impl $crate::CFType for $e {
            unsafe fn from_get_rule(cf: $crate::sys::CFTypeRef) -> Self {
                $crate::sys::CFRetain(cf);
                Self(cf as _)
            }

            unsafe fn from_create_rule(cf: $crate::sys::CFTypeRef) -> Self {
                Self(cf as _)
            }

            unsafe fn cf_type_ref(&self) -> $crate::sys::CFTypeRef {
                self.0 as _
            }
        }
    };
}
