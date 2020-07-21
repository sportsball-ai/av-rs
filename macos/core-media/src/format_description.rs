use super::sys;
use core_foundation::{result, CFType, OSStatus};

pub struct VideoFormatDescription(sys::CMVideoFormatDescriptionRef);
core_foundation::trait_impls!(VideoFormatDescription);

mod private {
    pub trait Sealed {}
}

pub trait FormatDescription: CFType + private::Sealed {}

impl VideoFormatDescription {
    /// Constructs a format description from H.264 parameter sets. Typically one SPS and one PPS
    /// are provided. They should consist of the raw NALU data, with emulation prevention bytes
    /// present, but no start code or length prefix. The NAL unit header length parameter
    /// corresponds to the length prefix size used for access unit NALUs (typically 4).
    pub fn with_h264_parameter_sets(parameter_sets: &[&[u8]], nal_unit_header_length: usize) -> Result<Self, OSStatus> {
        unsafe {
            let mut ret = std::ptr::null();
            let pointers: Vec<_> = parameter_sets.iter().map(|&ps| ps.as_ptr()).collect();
            let sizes: Vec<_> = parameter_sets.iter().map(|ps| ps.len() as u64).collect();
            result(
                sys::CMVideoFormatDescriptionCreateFromH264ParameterSets(
                    std::ptr::null_mut(),
                    parameter_sets.len() as _,
                    pointers.as_ptr(),
                    sizes.as_ptr(),
                    nal_unit_header_length as _,
                    &mut ret as _,
                )
                .into(),
            )?;
            Ok(Self(ret))
        }
    }
}

impl private::Sealed for VideoFormatDescription {}
impl FormatDescription for VideoFormatDescription {}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_format_description() {
        VideoFormatDescription::with_h264_parameter_sets(
            &[
                &[
                    0x67, 0x64, 0x00, 0x32, 0xAC, 0xB4, 0x02, 0x80, 0x2D, 0xD2, 0xA4, 0x00, 0x00, 0x0F, 0xA4, 0x00, 0x03, 0xA9, 0x85, 0x81, 0x00, 0x00, 0x63,
                    0x2E, 0x80, 0x01, 0x65, 0x0E, 0xF7, 0xBE, 0x17, 0x84, 0x42, 0x35,
                ],
                &[0x68, 0xeb, 0xe3, 0xcb, 0x22, 0xc0],
            ],
            4,
        )
        .unwrap();
    }
}
