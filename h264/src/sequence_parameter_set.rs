use super::{decode, syntax_elements::*, Bitstream, Decode};

use std::io;

// ITU-T H.264, 04/2017, 7.3.2.1.1
#[derive(Clone, Debug, Default)]
pub struct SequenceParameterSet {
    pub profile_idc: U8,
    pub constraint_set0_flag: U1,
    pub constraint_set1_flag: U1,
    pub constraint_set2_flag: U1,
    pub constraint_set3_flag: U1,
    pub constraint_set4_flag: U1,
    pub constraint_set5_flag: U1,
    pub reserved_zero_2bits: U2,
    pub level_idc: U8,
    pub seq_parameter_set_id: UE,

    /* if (
        profile_idc == 100 || profile_idc == 110 ||
        profile_idc == 122 || profile_idc == 244 || profile_idc == 44 ||
        profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
        profile_idc == 128 || profile_idc == 138 || profile_idc == 139 ||
        profile_idc == 134 || profile_idc == 135
    ) { */
    pub chroma_format_idc: UE,
    // if (chroma_format_idc == 3)
    pub separate_colour_plane_flag: U1,
    pub bit_depth_luma_minus8: UE,
    pub bit_depth_chroma_minus8: UE,
    pub qpprime_y_zero_transform_bypass_flag: U1,
    pub seq_scaling_matrix_present_flag: U1,
    /* } */
    pub log2_max_frame_num_minus4: UE,
    pub pic_order_cnt_type: UE,

    // if (pic_order_cnt_type == 0)
    pub log2_max_pic_order_cnt_lsb_minus4: UE,
    // else if (pic_order_cnt_type == 1) {
    pub delta_pic_order_always_zero_flag: U1,
    pub offset_for_non_ref_pic: SE,
    pub offset_for_top_to_bottom_field: SE,
    pub num_ref_frames_in_pic_order_cnt_cycle: UE,
    pub offset_for_ref_frame: Vec<SE>,
    // }
    pub max_num_ref_frames: UE,
    pub gaps_in_frame_num_value_allowed_flag: U1,
    pub pic_width_in_mbs_minus1: UE,
    pub pic_height_in_map_units_minus1: UE,
    pub frame_mbs_only_flag: U1,

    // if (!frame_mbs_only_flag)
    pub mb_adaptive_frame_field_flag: U1,

    pub direct_8x8_inference_flag: U1,
    pub frame_cropping_flag: U1,

    // if (frame_cropping_flag) {
    pub frame_crop_left_offset: UE,
    pub frame_crop_right_offset: UE,
    pub frame_crop_top_offset: UE,
    pub frame_crop_bottom_offset: UE,
    // }
    pub vui_parameters_present_flag: U1,

    // if (vui_parameters_present_flag) {
    pub vui_parameters: VUIParameters,
    // }
}

impl SequenceParameterSet {
    pub fn sub_width_c(&self) -> u16 {
        if self.chroma_format_idc.0 == 3 {
            1
        } else {
            2
        }
    }

    pub fn sub_height_c(&self) -> u16 {
        if self.chroma_format_idc.0 == 1 {
            2
        } else {
            1
        }
    }

    pub fn mb_width_c(&self) -> u16 {
        16 / self.sub_width_c()
    }

    pub fn mb_height_c(&self) -> u16 {
        16 / self.sub_height_c()
    }

    pub fn pic_width_in_mbs(&self) -> u64 {
        self.pic_width_in_mbs_minus1.0 + 1
    }

    pub fn pic_width_in_samples(&self) -> u64 {
        self.pic_width_in_mbs() * 16
    }

    pub fn pic_width_in_samples_c(&self) -> u64 {
        self.pic_width_in_mbs() * self.mb_width_c() as u64
    }

    pub fn pic_height_in_map_units(&self) -> u64 {
        self.pic_height_in_map_units_minus1.0 + 1
    }

    pub fn pic_size_in_map_units(&self) -> u64 {
        self.pic_width_in_mbs() * self.pic_height_in_map_units()
    }

    pub fn frame_height_in_mbs(&self) -> u64 {
        (2 - self.frame_mbs_only_flag.0 as u64) * self.pic_height_in_map_units()
    }

    pub fn chroma_array_type(&self) -> u64 {
        if self.separate_colour_plane_flag.0 != 0 {
            0
        } else {
            self.chroma_format_idc.0
        }
    }

    pub fn crop_unit_x(&self) -> u64 {
        if self.chroma_array_type() != 0 {
            self.sub_width_c() as _
        } else {
            1
        }
    }

    pub fn crop_unit_y(&self) -> u64 {
        (if self.chroma_array_type() != 0 { self.sub_height_c() as u64 } else { 1 }) * (2 - self.frame_mbs_only_flag.0 as u64)
    }

    pub fn frame_cropping_rectangle_left(&self) -> u64 {
        self.crop_unit_x() * self.frame_crop_left_offset.0
    }

    pub fn frame_cropping_rectangle_right(&self) -> u64 {
        self.pic_width_in_samples() - (self.crop_unit_x() * self.frame_crop_right_offset.0 + 1)
    }

    pub fn frame_cropping_rectangle_top(&self) -> u64 {
        self.crop_unit_y() * self.frame_crop_top_offset.0
    }

    pub fn frame_cropping_rectangle_bottom(&self) -> u64 {
        (16 * self.frame_height_in_mbs()) - (self.crop_unit_y() * self.frame_crop_bottom_offset.0 + 1)
    }

    pub fn frame_cropping_rectangle_width(&self) -> u64 {
        self.frame_cropping_rectangle_right() - self.frame_cropping_rectangle_left() + 1
    }

    pub fn frame_cropping_rectangle_height(&self) -> u64 {
        self.frame_cropping_rectangle_bottom() - self.frame_cropping_rectangle_top() + 1
    }
}

impl Decode for SequenceParameterSet {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(
            bs,
            &mut ret.profile_idc,
            &mut ret.constraint_set0_flag,
            &mut ret.constraint_set1_flag,
            &mut ret.constraint_set2_flag,
            &mut ret.constraint_set3_flag,
            &mut ret.constraint_set4_flag,
            &mut ret.constraint_set5_flag,
            &mut ret.reserved_zero_2bits,
            &mut ret.level_idc,
            &mut ret.seq_parameter_set_id
        )?;

        if ret.profile_idc.0 == 100
            || ret.profile_idc.0 == 110
            || ret.profile_idc.0 == 122
            || ret.profile_idc.0 == 244
            || ret.profile_idc.0 == 44
            || ret.profile_idc.0 == 83
            || ret.profile_idc.0 == 86
            || ret.profile_idc.0 == 118
            || ret.profile_idc.0 == 128
            || ret.profile_idc.0 == 138
            || ret.profile_idc.0 == 139
            || ret.profile_idc.0 == 134
            || ret.profile_idc.0 == 135
        {
            decode!(bs, &mut ret.chroma_format_idc)?;

            if ret.chroma_format_idc.0 == 3 {
                decode!(bs, &mut ret.separate_colour_plane_flag)?;
            }

            decode!(
                bs,
                &mut ret.bit_depth_luma_minus8,
                &mut ret.bit_depth_chroma_minus8,
                &mut ret.qpprime_y_zero_transform_bypass_flag,
                &mut ret.seq_scaling_matrix_present_flag
            )?;

            if ret.seq_scaling_matrix_present_flag.0 != 0 {
                return Err(io::Error::new(io::ErrorKind::Other, "decoding scaling matrices is not supported"));
            }
        } else {
            ret.chroma_format_idc.0 = 1;
        }

        decode!(bs, &mut ret.log2_max_frame_num_minus4, &mut ret.pic_order_cnt_type)?;

        if ret.pic_order_cnt_type.0 == 0 {
            decode!(bs, &mut ret.log2_max_pic_order_cnt_lsb_minus4)?;
        } else if ret.pic_order_cnt_type.0 == 1 {
            decode!(
                bs,
                &mut ret.delta_pic_order_always_zero_flag,
                &mut ret.offset_for_non_ref_pic,
                &mut ret.offset_for_top_to_bottom_field,
                &mut ret.num_ref_frames_in_pic_order_cnt_cycle
            )?;

            for _ in 0..ret.num_ref_frames_in_pic_order_cnt_cycle.0 {
                let offset = SE::decode(bs)?;
                ret.offset_for_ref_frame.push(offset);
            }
        }

        decode!(
            bs,
            &mut ret.max_num_ref_frames,
            &mut ret.gaps_in_frame_num_value_allowed_flag,
            &mut ret.pic_width_in_mbs_minus1,
            &mut ret.pic_height_in_map_units_minus1,
            &mut ret.frame_mbs_only_flag
        )?;

        if ret.frame_mbs_only_flag.0 == 0 {
            decode!(bs, &mut ret.mb_adaptive_frame_field_flag)?;
        }

        decode!(bs, &mut ret.direct_8x8_inference_flag, &mut ret.frame_cropping_flag)?;

        if ret.frame_cropping_flag.0 != 0 {
            decode!(
                bs,
                &mut ret.frame_crop_left_offset,
                &mut ret.frame_crop_right_offset,
                &mut ret.frame_crop_top_offset,
                &mut ret.frame_crop_bottom_offset
            )?;
        }

        decode!(bs, &mut ret.vui_parameters_present_flag)?;

        if ret.vui_parameters_present_flag.0 != 0 {
            decode!(bs, &mut ret.vui_parameters)?;
        }

        Ok(ret)
    }
}

#[derive(Clone, Debug, Default)]
pub struct VUIParameters {
    pub aspect_ratio_info_present_flag: U1,

    // if (aspect_ratio_info_present_flag) {
    pub aspect_ratio_idc: U8,
    // if (aspect_ratio_idc == Extended_SAR) {
    pub sar_width: U16,
    pub sar_height: U16,
    // }
    // }
    pub overscan_info_present_flag: U1,

    // if (overscan_info_present_flag) {
    pub overscan_appropriate_flag: U1,
    // }
    pub video_signal_type_present_flag: U1,

    // if (video_signal_type_present_flag) {
    pub video_format: U3,
    pub video_full_range_flag: U1,
    pub colour_description_present_flag: U1,

    // if (colour_description_present_flag) {
    pub colour_primaries: U8,
    pub transfer_characteristics: U8,
    pub matrix_coefficients: U8,
    // }
    // }
    pub chroma_loc_info_present_flag: U1,

    // if (chroma_loc_info_present_flag) {
    pub chroma_sample_loc_type_top_field: UE,
    pub chroma_sample_loc_type_bottom_field: UE,
    // }
    pub timing_info_present_flag: U1,

    // if (timing_info_present_flag) {
    pub num_units_in_tick: U32,
    pub time_scale: U32,
    pub fixed_frame_rate_flag: U1,
    // }
    pub nal_hrd_parameters_present_flag: U1,
    // if (nal_hrd_parameters_present_flag) {
    pub nal_hrd_parameters: Option<HRDParameters>,
    // }
    pub vcl_hrd_parameters_present_flag: U1,
    // if (vcl_hrd_parameters_present_flag) {
    pub vcl_hrd_parameters: Option<HRDParameters>,
    // }

    // if (nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag)
    pub low_delay_hrd_flag: U1,
    // }
    pub pic_struct_present_flag: U1,
}

pub const ASPECT_RATIO_IDC_EXTENDED_SAR: u8 = 255;

impl VUIParameters {
    pub fn cpb_dpb_delays_present_flag(&self) -> bool {
        self.nal_hrd_parameters_present_flag.0 != 0 || self.vcl_hrd_parameters_present_flag.0 != 0
    }
}

impl Decode for VUIParameters {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(bs, &mut ret.aspect_ratio_info_present_flag)?;

        if ret.aspect_ratio_info_present_flag.0 != 0 {
            decode!(bs, &mut ret.aspect_ratio_idc)?;

            if ret.aspect_ratio_idc.0 == ASPECT_RATIO_IDC_EXTENDED_SAR {
                decode!(bs, &mut ret.sar_width, &mut ret.sar_height)?;
            }
        }

        decode!(bs, &mut ret.overscan_info_present_flag)?;

        if ret.overscan_info_present_flag.0 != 0 {
            decode!(bs, &mut ret.overscan_appropriate_flag)?;
        }

        decode!(bs, &mut ret.video_signal_type_present_flag)?;

        if ret.video_signal_type_present_flag.0 != 0 {
            decode!(
                bs,
                &mut ret.video_format,
                &mut ret.video_full_range_flag,
                &mut ret.colour_description_present_flag
            )?;

            if ret.colour_description_present_flag.0 != 0 {
                decode!(bs, &mut ret.colour_primaries, &mut ret.transfer_characteristics, &mut ret.matrix_coefficients)?;
            }
        }

        decode!(bs, &mut ret.chroma_loc_info_present_flag)?;

        if ret.chroma_loc_info_present_flag.0 != 0 {
            decode!(bs, &mut ret.chroma_sample_loc_type_top_field, &mut ret.chroma_sample_loc_type_bottom_field)?;
        }

        decode!(bs, &mut ret.timing_info_present_flag)?;

        if ret.timing_info_present_flag.0 != 0 {
            decode!(bs, &mut ret.num_units_in_tick, &mut ret.time_scale, &mut ret.fixed_frame_rate_flag)?;
        }

        decode!(bs, &mut ret.nal_hrd_parameters_present_flag)?;
        if ret.nal_hrd_parameters_present_flag.0 != 0 {
            ret.nal_hrd_parameters = Some(HRDParameters::decode(bs)?);
        }

        decode!(bs, &mut ret.vcl_hrd_parameters_present_flag)?;
        if ret.vcl_hrd_parameters_present_flag.0 != 0 {
            ret.vcl_hrd_parameters = Some(HRDParameters::decode(bs)?);
        }

        if ret.nal_hrd_parameters_present_flag.0 != 0 || ret.vcl_hrd_parameters_present_flag.0 != 0 {
            decode!(bs, &mut ret.low_delay_hrd_flag)?;
        }

        decode!(bs, &mut ret.pic_struct_present_flag)?;

        Ok(ret)
    }
}

#[derive(Clone, Debug, Default)]
pub struct HRDParameters {
    pub cpb_cnt_minus1: UE,
    pub bit_rate_scale: U4,
    pub cpb_size_scale: U4,
    pub sei_scheds: Vec<SEISched>,
    pub initial_cpb_removal_delay_length_minus1: U5,
    pub cpb_removal_delay_length_minus1: U5,
    pub dpb_output_delay_length_minus1: U5,
    pub time_offset_length: U5,
}

impl Decode for HRDParameters {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(bs, &mut ret.cpb_cnt_minus1, &mut ret.bit_rate_scale, &mut ret.cpb_size_scale)?;

        for _ in 0..=ret.cpb_cnt_minus1.0 {
            ret.sei_scheds.push(SEISched::decode(bs)?);
        }

        decode!(
            bs,
            &mut ret.initial_cpb_removal_delay_length_minus1,
            &mut ret.cpb_removal_delay_length_minus1,
            &mut ret.dpb_output_delay_length_minus1,
            &mut ret.time_offset_length
        )?;

        Ok(ret)
    }
}

#[derive(Clone, Debug, Default)]
pub struct SEISched {
    pub bit_rate_value_minus1: UE,
    pub cpb_size_value_minus1: UE,
    pub cbr_flag: U1,
}

impl Decode for SEISched {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();
        decode!(bs, &mut ret.bit_rate_value_minus1, &mut ret.cpb_size_value_minus1, &mut ret.cbr_flag)?;
        Ok(ret)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_sequence_parameter_set() {
        let mut bs = Bitstream::new(vec![
            0x4d, 0x40, 0x1f, 0xec, 0xa0, 0x28, 0x02, 0xdd, 0x80, 0xb5, 0x01, 0x01, 0x01, 0x40, 0x00, 0x00, 0x00, 0x40, 0x00, 0x05, 0xdc, 0x03, 0xc6, 0x0c,
            0x65, 0x80,
        ]);

        let sps = SequenceParameterSet::decode(&mut bs).unwrap();

        assert_eq!(77, sps.profile_idc.0);
        assert_eq!(0, sps.constraint_set0_flag.0);
        assert_eq!(1, sps.constraint_set1_flag.0);
        assert_eq!(0, sps.constraint_set2_flag.0);
        assert_eq!(0, sps.constraint_set3_flag.0);
        assert_eq!(0, sps.constraint_set4_flag.0);
        assert_eq!(0, sps.constraint_set5_flag.0);
        assert_eq!(0, sps.reserved_zero_2bits.0);
        assert_eq!(31, sps.level_idc.0);

        assert_eq!(0, sps.log2_max_frame_num_minus4.0);
        assert_eq!(0, sps.pic_order_cnt_type.0);
        assert_eq!(2, sps.log2_max_pic_order_cnt_lsb_minus4.0);

        assert_eq!(4, sps.max_num_ref_frames.0);
        assert_eq!(0, sps.gaps_in_frame_num_value_allowed_flag.0);
        assert_eq!(79, sps.pic_width_in_mbs_minus1.0);
        assert_eq!(44, sps.pic_height_in_map_units_minus1.0);
        assert_eq!(1, sps.frame_mbs_only_flag.0);

        assert_eq!(1, sps.direct_8x8_inference_flag.0);
        assert_eq!(0, sps.frame_cropping_flag.0);

        assert_eq!(1280, sps.frame_cropping_rectangle_width());
        assert_eq!(720, sps.frame_cropping_rectangle_height());

        assert_eq!(1, sps.vui_parameters_present_flag.0);

        assert_eq!(1, sps.vui_parameters.timing_info_present_flag.0);
        assert_eq!(1, sps.vui_parameters.num_units_in_tick.0);
        assert_eq!(6000, sps.vui_parameters.time_scale.0);
        assert_eq!(0, sps.vui_parameters.fixed_frame_rate_flag.0);
    }
}
