use std::convert::TryFrom;

use crate::{sys, Error, ResultExt};
use cf::{MutableDictionary, StringRef};
use core_foundation::{self as cf, base::result, Array, CFType, Dictionary};

#[repr(transparent)]
pub struct Encoder(Dictionary);

impl Encoder {
    pub fn name(&self) -> cf::StringRef {
        unsafe { self.0.cf_type_value(sys::kVTVideoEncoderList_EncoderName as _) }.expect("encoder name should be set")
    }

    pub fn id(&self) -> cf::StringRef {
        unsafe { self.0.cf_type_value(sys::kVTVideoEncoderList_EncoderID as _) }.expect("encoder id should be set")
    }

    pub fn codec_type(&self) -> cf::Number {
        unsafe { self.0.cf_type_value(sys::kVTVideoEncoderList_CodecType as _) }.expect("encoder id should be set")
    }

    pub fn supported_properties(&self, width: i32, height: i32) -> Result<Vec<(StringRef, Dictionary)>, Error> {
        let id = self.id();
        let codec_type = u32::try_from(self.codec_type()).expect("codec_type is a u32");
        let mut spec = MutableDictionary::new_cf_type();
        let mut out = std::ptr::null_mut();
        unsafe {
            spec.set_value(sys::kVTVideoEncoderSpecification_EncoderID as _, id.cf_type_ref());
            result(
                sys::VTCopySupportedPropertyDictionaryForEncoder(
                    width,
                    height,
                    codec_type,
                    spec.cf_type_ref() as _,
                    std::ptr::null_mut(),
                    &mut out as *mut _ as _,
                )
                .into(),
            )
            .context("VTCopySupportedPropertyDictionaryForEncoder")?;
            let dict = Dictionary::from_create_rule(out);
            Ok(dict.get_keys_and_values())
        }
    }
}

pub struct EncoderList(Array);

impl EncoderList {
    /// Returns an encoder list via `VTCopyVideoEncoderList`.
    pub fn copy() -> Result<Self, Error> {
        unsafe {
            let mut v = std::ptr::null();
            result(sys::VTCopyVideoEncoderList(std::ptr::null(), &mut v).into()).context("VTCopyVideoEncoderList")?;
            Ok(Self(Array::from_create_rule(v as _)))
        }
    }

    #[inline]
    pub fn len(&self) -> usize {
        self.0.len()
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    pub fn get(&self, idx: usize) -> Encoder {
        assert!(idx < self.len());
        Encoder(unsafe { self.0.cf_type_value_at_index(idx) })
    }
}
