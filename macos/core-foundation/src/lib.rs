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

#[macro_export]
macro_rules! trait_impls {
    ($e:ident) => {
        impl std::fmt::Debug for $e {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                use core_foundation::CFType;
                write!(f, "{}", self.description().unwrap_or("{..}".to_string()))
            }
        }

        impl Drop for $e {
            fn drop(&mut self) {
                unsafe { core_foundation::sys::CFRelease(self.0 as _) }
            }
        }

        unsafe impl Send for $e {}

        impl core_foundation::CFType for $e {
            unsafe fn with_cf_type_ref(cf: core_foundation::sys::CFTypeRef) -> Self {
                core_foundation::sys::CFRetain(cf);
                Self(cf as _)
            }

            unsafe fn cf_type_ref(&self) -> core_foundation::sys::CFTypeRef {
                self.0 as _
            }
        }
    };
}
