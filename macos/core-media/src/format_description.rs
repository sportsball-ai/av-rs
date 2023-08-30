use super::sys;
use core_foundation::{result, CFType, OSStatus};

pub struct FormatDescription(sys::CMFormatDescriptionRef);
core_foundation::trait_impls!(FormatDescription);

pub struct FormatDescriptionH264ParameterSet<'a> {
    pub data: &'a [u8],
    pub nal_unit_header_length: usize,
}

pub struct FormatDescriptionHevcParameterSet<'a> {
    pub data: &'a [u8],
    pub nal_unit_header_length: usize,
}

impl FormatDescription {
    pub fn h264_parameter_set_at_index(&self, idx: usize) -> Result<FormatDescriptionH264ParameterSet, OSStatus> {
        unsafe {
            let mut nal_unit_header_length = 0;
            let mut ptr = std::ptr::null();
            let mut len = 0;
            result(
                sys::CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                    self.0,
                    idx as _,
                    &mut ptr,
                    &mut len,
                    std::ptr::null_mut(),
                    &mut nal_unit_header_length,
                )
                .into(),
            )?;
            Ok(FormatDescriptionH264ParameterSet {
                data: std::slice::from_raw_parts(ptr as _, len as _),
                nal_unit_header_length: nal_unit_header_length as _,
            })
        }
    }

    pub fn hevc_parameter_set_at_index(&self, idx: usize) -> Result<FormatDescriptionHevcParameterSet, OSStatus> {
        unsafe {
            let mut nal_unit_header_length = 0;
            let mut ptr = std::ptr::null();
            let mut len = 0;
            result(
                sys::CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                    self.0,
                    idx as _,
                    &mut ptr,
                    &mut len,
                    std::ptr::null_mut(),
                    &mut nal_unit_header_length,
                )
                .into(),
            )?;
            Ok(FormatDescriptionHevcParameterSet {
                data: std::slice::from_raw_parts(ptr as _, len as _),
                nal_unit_header_length: nal_unit_header_length as _,
            })
        }
    }
}

pub struct VideoFormatDescription(sys::CMVideoFormatDescriptionRef);
core_foundation::trait_impls!(VideoFormatDescription);

impl VideoFormatDescription {
    /// Constructs a format description from H.264 parameter sets. Typically one SPS and one PPS
    /// are provided. They should consist of the raw NALU data, with emulation prevention bytes
    /// present, but no start code or length prefix. The NAL unit header length parameter
    /// corresponds to the length prefix size used for access unit NALUs (typically 4).
    pub fn with_h264_parameter_sets(parameter_sets: &[&[u8]], nal_unit_header_length: usize) -> Result<Self, OSStatus> {
        unsafe {
            let mut ret = std::ptr::null();
            let pointers: Vec<_> = parameter_sets.iter().map(|&ps| ps.as_ptr()).collect();
            let sizes: Vec<_> = parameter_sets.iter().map(|ps| ps.len()).collect();
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

    /// Constructs a format description from H.265 parameter sets: typically one each of SPS,
    /// PPS, and VPS. They should consist of the raw NALU data, with emulation prevention bytes
    /// present, but no start code or length prefix. The NAL unit header length parameter
    /// corresponds to the length prefix size used for access unit NALUs (typically 4).
    pub fn with_hevc_parameter_sets(parameter_sets: &[&[u8]], nal_unit_header_length: usize) -> Result<Self, OSStatus> {
        unsafe {
            let mut ret = std::ptr::null();
            let pointers: Vec<_> = parameter_sets.iter().map(|&ps| ps.as_ptr()).collect();
            let sizes: Vec<_> = parameter_sets.iter().map(|ps| ps.len()).collect();
            result(
                sys::CMVideoFormatDescriptionCreateFromHEVCParameterSets(
                    std::ptr::null_mut(),
                    parameter_sets.len() as _,
                    pointers.as_ptr(),
                    sizes.as_ptr(),
                    nal_unit_header_length as _,
                    std::ptr::null(),
                    &mut ret as _,
                )
                .into(),
            )?;
            Ok(Self(ret))
        }
    }
}

impl From<VideoFormatDescription> for FormatDescription {
    fn from(desc: VideoFormatDescription) -> Self {
        unsafe { Self::from_get_rule(desc.0 as _) }
    }
}

impl From<&VideoFormatDescription> for FormatDescription {
    fn from(desc: &VideoFormatDescription) -> Self {
        unsafe { Self::from_get_rule(desc.0 as _) }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_video_format_description() {
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
