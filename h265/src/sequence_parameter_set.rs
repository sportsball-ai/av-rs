use super::{decode, encode, syntax_elements::*, Bitstream, BitstreamWriter, Decode, Encode, ProfileTierLevel};
use std::io;

#[derive(Clone, Debug, Default)]
pub struct SequenceParameterSetSubLayerOrderingInfo {
    pub sps_max_dec_pic_buffering_minus1: UE,
    pub sps_max_num_reorder_pics: UE,
    pub sps_max_latency_increase_plus1: UE,
}

impl Decode for SequenceParameterSetSubLayerOrderingInfo {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        Ok(Self {
            sps_max_dec_pic_buffering_minus1: UE::decode(bs)?,
            sps_max_num_reorder_pics: UE::decode(bs)?,
            sps_max_latency_increase_plus1: UE::decode(bs)?,
        })
    }
}

impl Encode for SequenceParameterSetSubLayerOrderingInfo {
    fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>) -> io::Result<()> {
        encode!(
            bs,
            &self.sps_max_dec_pic_buffering_minus1,
            &self.sps_max_num_reorder_pics,
            &self.sps_max_latency_increase_plus1
        )
    }
}

#[derive(Clone, Debug, Default)]
pub struct ShortTermRefPicSet {
    // if( stRpsIdx != 0 )
    pub inter_ref_pic_set_prediction_flag: U1,

    pub num_negative_pics: UE,
    pub num_positive_pics: UE,

    pub delta_poc_s0_minus1: Vec<UE>,
    pub used_by_curr_pic_s0_flag: Vec<U1>,

    pub delta_poc_s1_minus1: Vec<UE>,
    pub used_by_curr_pic_s1_flag: Vec<U1>,
}

#[allow(non_snake_case)]
impl ShortTermRefPicSet {
    pub fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>, st_rps_idx: u64) -> io::Result<Self> {
        let mut ret = Self::default();

        if st_rps_idx != 0 {
            decode!(bs, &mut ret.inter_ref_pic_set_prediction_flag)?;
        }

        if ret.inter_ref_pic_set_prediction_flag.0 != 0 {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                "short term ref picture set source candidates are not supported",
            ));
        } else {
            decode!(bs, &mut ret.num_negative_pics, &mut ret.num_positive_pics)?;
            for _ in 0..ret.num_negative_pics.0 {
                ret.delta_poc_s0_minus1.push(UE::decode(bs)?);
                ret.used_by_curr_pic_s0_flag.push(U1::decode(bs)?);
            }
            for _ in 0..ret.num_positive_pics.0 {
                ret.delta_poc_s1_minus1.push(UE::decode(bs)?);
                ret.used_by_curr_pic_s1_flag.push(U1::decode(bs)?);
            }
        }

        Ok(ret)
    }

    pub fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>, st_rps_idx: u64) -> io::Result<()> {
        if st_rps_idx != 0 {
            encode!(bs, &self.inter_ref_pic_set_prediction_flag)?;
        }

        if self.inter_ref_pic_set_prediction_flag.0 != 0 {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                "short term ref picture set source candidates are not supported",
            ));
        } else {
            encode!(bs, &self.num_negative_pics, &self.num_positive_pics)?;
            for i in 0..self.delta_poc_s0_minus1.len() {
                encode!(bs, &self.delta_poc_s0_minus1[i], &self.used_by_curr_pic_s0_flag[i])?;
            }
            for i in 0..self.delta_poc_s1_minus1.len() {
                encode!(bs, &self.delta_poc_s1_minus1[i], &self.used_by_curr_pic_s1_flag[i])?;
            }
        }

        Ok(())
    }

    pub fn NumNegativePics(&self, _stRpsIdx: usize) -> u64 {
        self.num_negative_pics.0
    }

    pub fn NumPositivePics(&self, _stRpsIdx: usize) -> u64 {
        self.num_positive_pics.0
    }

    pub fn UsedByCurrPicS0(&self, _stRpsIdx: usize, i: usize) -> bool {
        self.used_by_curr_pic_s0_flag[i].0 != 0
    }

    pub fn UsedByCurrPicS1(&self, _stRpsIdx: usize, i: usize) -> bool {
        self.used_by_curr_pic_s1_flag[i].0 != 0
    }
}

// ITU-T H.265, 11/2019 7.3.2.1.1
#[derive(Clone, Debug, Default)]
pub struct SequenceParameterSet {
    pub sps_video_parameter_set_id: U4,
    pub sps_max_sub_layers_minus1: U3,
    pub sps_temporal_id_nesting_flag: U1,
    pub profile_tier_level: ProfileTierLevel,
    pub sps_seq_parameter_set_id: UE,
    pub chroma_format_idc: UE,

    // if( chroma_format_idc = = 3 )
    pub separate_colour_plane_flag: U1,

    pub pic_width_in_luma_samples: UE,
    pub pic_height_in_luma_samples: UE,
    pub conformance_window_flag: U1,

    // if( conformance_window_flag ) {
    pub conf_win_left_offset: UE,
    pub conf_win_right_offset: UE,
    pub conf_win_top_offset: UE,
    pub conf_win_bottom_offset: UE,

    pub bit_depth_luma_minus8: UE,
    pub bit_depth_chroma_minus8: UE,
    pub log2_max_pic_order_cnt_lsb_minus4: UE,
    pub sps_sub_layer_ordering_info_present_flag: U1,

    // for( i = ( sps_sub_layer_ordering_info_present_flag ? 0 : sps_max_sub_layers_minus1 ); i <= sps_max_sub_layers_minus1; i++ )
    pub sub_layer_ordering_info: Vec<SequenceParameterSetSubLayerOrderingInfo>,

    pub log2_min_luma_coding_block_size_minus3: UE,
    pub log2_diff_max_min_luma_coding_block_size: UE,
    pub log2_min_luma_transform_block_size_minus2: UE,
    pub log2_diff_max_min_luma_transform_block_size: UE,
    pub max_transform_hierarchy_depth_inter: UE,
    pub max_transform_hierarchy_depth_intra: UE,
    pub scaling_list_enabled_flag: U1,

    // if( scaling_list_enabled_flag ) {
    pub sps_scaling_list_data_present_flag: U1,
    // if( sps_scaling_list_data_present_flag )
    // TODO: support scaling matrices?
    //}
    pub amp_enabled_flag: U1,
    pub sample_adaptive_offset_enabled_flag: U1,
    pub pcm_enabled_flag: U1,

    // if( pcm_enabled_flag ) {
    pub pcm_sample_bit_depth_luma_minus1: U4,
    pub pcm_sample_bit_depth_chroma_minus1: U4,
    pub log2_min_pcm_luma_coding_block_size_minus3: UE,
    pub log2_diff_max_min_pcm_luma_coding_block_size: UE,
    pub pcm_loop_filter_disabled_flag: U1,
    // }
    pub num_short_term_ref_pic_sets: UE,

    // for( i = 0; i < num_short_term_ref_pic_sets; i++)
    pub st_ref_pic_set: Vec<ShortTermRefPicSet>,

    pub long_term_ref_pics_present_flag: U1,

    // if( long_term_ref_pics_present_flag ) {
    pub num_long_term_ref_pics_sps: UE,
    // for( i = 0; i < num_long_term_ref_pics_sps; i++ ) {
    pub lt_ref_pic_poc_lsb_sps: Vec<u64>,
    pub used_by_curr_pic_lt_sps_flag: Vec<U1>,
    // }
    // }
    pub sps_temporal_mvp_enabled_flag: U1,
    pub strong_intra_smoothing_enabled_flag: U1,
    pub vui_parameters_present_flag: U1,

    // if( vui_parameters_present_flag )
    pub vui_parameters: VUIParameters,

    // XXX: VUIParameters does not yet parse all fields. extensions cannot be added until it's completed
    pub remaining_bits: Vec<U1>,
}

// These function names follow the naming conventions in the standard.
#[allow(non_snake_case)]
impl SequenceParameterSet {
    pub fn MinCbLog2SizeY(&self) -> u64 {
        self.log2_min_luma_coding_block_size_minus3.0 + 3
    }

    pub fn CtbLog2SizeY(&self) -> u64 {
        self.MinCbLog2SizeY() + self.log2_diff_max_min_luma_coding_block_size.0
    }

    pub fn MinCbSizeY(&self) -> u64 {
        1 << self.MinCbLog2SizeY()
    }

    pub fn CtbSizeY(&self) -> u64 {
        1 << self.CtbLog2SizeY()
    }

    pub fn PicWidthInMinCbsY(&self) -> u64 {
        self.pic_width_in_luma_samples.0 / self.MinCbSizeY()
    }

    pub fn PicWidthInCtbsY(&self) -> u64 {
        let ctb_size_y = self.CtbSizeY();
        (self.pic_width_in_luma_samples.0 + ctb_size_y - 1) / ctb_size_y
    }

    pub fn PicHeightInMinCbsY(&self) -> u64 {
        self.pic_height_in_luma_samples.0 / self.MinCbSizeY()
    }

    pub fn PicHeightInCtbsY(&self) -> u64 {
        let ctb_size_y = self.CtbSizeY();
        (self.pic_height_in_luma_samples.0 + ctb_size_y - 1) / ctb_size_y
    }

    pub fn PicSizeInMinCbsY(&self) -> u64 {
        self.PicWidthInMinCbsY() * self.PicHeightInMinCbsY()
    }

    pub fn PicSizeInCtbsY(&self) -> u64 {
        self.PicWidthInCtbsY() * self.PicHeightInCtbsY()
    }

    pub fn ChromaArrayType(&self) -> u64 {
        match self.separate_colour_plane_flag.0 {
            0 => self.chroma_format_idc.0,
            _ => 0,
        }
    }

    pub fn SubWidthC(&self) -> u64 {
        match (self.chroma_format_idc.0, self.separate_colour_plane_flag.0) {
            (1, 0) | (2, 0) => 2,
            _ => 1,
        }
    }

    pub fn SubHeightC(&self) -> u64 {
        match (self.chroma_format_idc.0, self.separate_colour_plane_flag.0) {
            (1, 0) => 2,
            _ => 1,
        }
    }

    pub fn croppedWidth(&self) -> u64 {
        self.pic_width_in_luma_samples.0 - self.SubWidthC() * (self.conf_win_left_offset.0 + self.conf_win_right_offset.0)
    }

    pub fn croppedHeight(&self) -> u64 {
        self.pic_height_in_luma_samples.0 - self.SubHeightC() * (self.conf_win_top_offset.0 + self.conf_win_bottom_offset.0)
    }
}

impl Decode for SequenceParameterSet {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(
            bs,
            &mut ret.sps_video_parameter_set_id,
            &mut ret.sps_max_sub_layers_minus1,
            &mut ret.sps_temporal_id_nesting_flag
        )?;

        ret.profile_tier_level = ProfileTierLevel::decode(bs, 1, ret.sps_max_sub_layers_minus1.0)?;

        decode!(bs, &mut ret.sps_seq_parameter_set_id, &mut ret.chroma_format_idc)?;

        if ret.chroma_format_idc.0 == 3 {
            decode!(bs, &mut ret.separate_colour_plane_flag)?;
        }

        decode!(
            bs,
            &mut ret.pic_width_in_luma_samples,
            &mut ret.pic_height_in_luma_samples,
            &mut ret.conformance_window_flag
        )?;

        if ret.conformance_window_flag.0 != 0 {
            decode!(
                bs,
                &mut ret.conf_win_left_offset,
                &mut ret.conf_win_right_offset,
                &mut ret.conf_win_top_offset,
                &mut ret.conf_win_bottom_offset
            )?;
        }

        decode!(
            bs,
            &mut ret.bit_depth_luma_minus8,
            &mut ret.bit_depth_chroma_minus8,
            &mut ret.log2_max_pic_order_cnt_lsb_minus4,
            &mut ret.sps_sub_layer_ordering_info_present_flag
        )?;

        let mut i = match ret.sps_sub_layer_ordering_info_present_flag.0 {
            0 => ret.sps_max_sub_layers_minus1.0,
            _ => 0,
        };
        while i <= ret.sps_max_sub_layers_minus1.0 {
            ret.sub_layer_ordering_info.push(SequenceParameterSetSubLayerOrderingInfo::decode(bs)?);
            i += 1;
        }

        decode!(
            bs,
            &mut ret.log2_min_luma_coding_block_size_minus3,
            &mut ret.log2_diff_max_min_luma_coding_block_size,
            &mut ret.log2_min_luma_transform_block_size_minus2,
            &mut ret.log2_diff_max_min_luma_transform_block_size,
            &mut ret.max_transform_hierarchy_depth_inter,
            &mut ret.max_transform_hierarchy_depth_intra,
            &mut ret.scaling_list_enabled_flag
        )?;

        if ret.scaling_list_enabled_flag.0 != 0 {
            decode!(bs, &mut ret.sps_scaling_list_data_present_flag)?;
            if ret.sps_scaling_list_data_present_flag.0 != 0 {
                return Err(io::Error::new(io::ErrorKind::Other, "decoding scaling matrices is not supported"));
            }
        }

        decode!(
            bs,
            &mut ret.amp_enabled_flag,
            &mut ret.sample_adaptive_offset_enabled_flag,
            &mut ret.pcm_enabled_flag
        )?;

        if ret.pcm_enabled_flag.0 != 0 {
            decode!(
                bs,
                &mut ret.pcm_sample_bit_depth_luma_minus1,
                &mut ret.pcm_sample_bit_depth_chroma_minus1,
                &mut ret.log2_min_pcm_luma_coding_block_size_minus3,
                &mut ret.log2_diff_max_min_pcm_luma_coding_block_size,
                &mut ret.pcm_loop_filter_disabled_flag
            )?;
        }

        decode!(bs, &mut ret.num_short_term_ref_pic_sets)?;

        for i in 0..ret.num_short_term_ref_pic_sets.0 {
            ret.st_ref_pic_set.push(ShortTermRefPicSet::decode(bs, i)?);
        }

        decode!(bs, &mut ret.long_term_ref_pics_present_flag)?;

        if ret.long_term_ref_pics_present_flag.0 != 0 {
            decode!(bs, &mut ret.num_long_term_ref_pics_sps)?;
            for _ in 0..ret.num_long_term_ref_pics_sps.0 {
                ret.lt_ref_pic_poc_lsb_sps
                    .push(bs.read_bits(ret.log2_max_pic_order_cnt_lsb_minus4.0 as usize + 4)?);
                ret.used_by_curr_pic_lt_sps_flag.push(U1::decode(bs)?);
            }
        }

        decode!(
            bs,
            &mut ret.sps_temporal_mvp_enabled_flag,
            &mut ret.strong_intra_smoothing_enabled_flag,
            &mut ret.vui_parameters_present_flag
        )?;

        if ret.vui_parameters_present_flag.0 != 0 {
            decode!(bs, &mut ret.vui_parameters)?;
        }

        ret.remaining_bits = bs.bits().map(|b| U1(b as _)).collect();

        Ok(ret)
    }
}

impl Encode for SequenceParameterSet {
    fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>) -> io::Result<()> {
        encode!(
            bs,
            &self.sps_video_parameter_set_id,
            &self.sps_max_sub_layers_minus1,
            &self.sps_temporal_id_nesting_flag
        )?;

        self.profile_tier_level.encode(bs, 1, self.sps_max_sub_layers_minus1.0)?;

        encode!(bs, &self.sps_seq_parameter_set_id, &self.chroma_format_idc)?;

        if self.chroma_format_idc.0 == 3 {
            encode!(bs, &self.separate_colour_plane_flag)?;
        }

        encode!(
            bs,
            &self.pic_width_in_luma_samples,
            &self.pic_height_in_luma_samples,
            &self.conformance_window_flag
        )?;

        if self.conformance_window_flag.0 != 0 {
            encode!(
                bs,
                &self.conf_win_left_offset,
                &self.conf_win_right_offset,
                &self.conf_win_top_offset,
                &self.conf_win_bottom_offset
            )?;
        }

        encode!(
            bs,
            &self.bit_depth_luma_minus8,
            &self.bit_depth_chroma_minus8,
            &self.log2_max_pic_order_cnt_lsb_minus4,
            &self.sps_sub_layer_ordering_info_present_flag
        )?;

        let mut i = match self.sps_sub_layer_ordering_info_present_flag.0 {
            0 => self.sps_max_sub_layers_minus1.0,
            _ => 0,
        };
        while i <= self.sps_max_sub_layers_minus1.0 {
            self.sub_layer_ordering_info.encode(bs)?;
            i += 1;
        }

        encode!(
            bs,
            &self.log2_min_luma_coding_block_size_minus3,
            &self.log2_diff_max_min_luma_coding_block_size,
            &self.log2_min_luma_transform_block_size_minus2,
            &self.log2_diff_max_min_luma_transform_block_size,
            &self.max_transform_hierarchy_depth_inter,
            &self.max_transform_hierarchy_depth_intra,
            &self.scaling_list_enabled_flag
        )?;

        if self.scaling_list_enabled_flag.0 != 0 {
            encode!(bs, &self.sps_scaling_list_data_present_flag)?;
            if self.sps_scaling_list_data_present_flag.0 != 0 {
                return Err(io::Error::new(io::ErrorKind::Other, "decoding scaling matrices is not supported"));
            }
        }

        encode!(bs, &self.amp_enabled_flag, &self.sample_adaptive_offset_enabled_flag, &self.pcm_enabled_flag)?;

        if self.pcm_enabled_flag.0 != 0 {
            encode!(
                bs,
                &self.pcm_sample_bit_depth_luma_minus1,
                &self.pcm_sample_bit_depth_chroma_minus1,
                &self.log2_min_pcm_luma_coding_block_size_minus3,
                &self.log2_diff_max_min_pcm_luma_coding_block_size,
                &self.pcm_loop_filter_disabled_flag
            )?;
        }

        encode!(bs, &self.num_short_term_ref_pic_sets)?;

        for i in 0..self.num_short_term_ref_pic_sets.0 {
            self.st_ref_pic_set[i as usize].encode(bs, i)?;
        }

        encode!(bs, &self.long_term_ref_pics_present_flag)?;

        if self.long_term_ref_pics_present_flag.0 != 0 {
            encode!(bs, &self.num_long_term_ref_pics_sps)?;
            for i in 0..self.num_long_term_ref_pics_sps.0 {
                bs.write_bits(self.lt_ref_pic_poc_lsb_sps[i as usize], self.log2_max_pic_order_cnt_lsb_minus4.0 as usize + 4)?;
                self.used_by_curr_pic_lt_sps_flag[i as usize].encode(bs)?;
            }
        }

        encode!(
            bs,
            &self.sps_temporal_mvp_enabled_flag,
            &self.strong_intra_smoothing_enabled_flag,
            &self.vui_parameters_present_flag
        )?;

        if self.vui_parameters_present_flag.0 != 0 {
            encode!(bs, &self.vui_parameters)?;
        }

        self.remaining_bits.encode(bs)?;

        Ok(())
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
    pub neutral_chroma_indication_flag: U1,
    pub field_seq_flag: U1,
    pub frame_field_info_present_flag: U1,
    pub default_display_window_flag: U1,

    // if( default_display_window_flag ) {
    pub def_disp_win_left_offset: UE,
    pub def_disp_win_right_offset: UE,
    pub def_disp_win_top_offset: UE,
    pub def_disp_win_bottom_offset: UE,
    // }
    pub vui_timing_info_present_flag: U1,

    // if (vui_timing_info_present_flag) {
    pub vui_num_units_in_tick: U32,
    pub vui_time_scale: U32,
    // TODO: decode remaining fields?
    // }
}

pub const ASPECT_RATIO_IDC_EXTENDED_SAR: u8 = 255;

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

        decode!(
            bs,
            &mut ret.neutral_chroma_indication_flag,
            &mut ret.field_seq_flag,
            &mut ret.frame_field_info_present_flag,
            &mut ret.default_display_window_flag
        )?;

        if ret.default_display_window_flag.0 != 0 {
            decode!(
                bs,
                &mut ret.def_disp_win_left_offset,
                &mut ret.def_disp_win_right_offset,
                &mut ret.def_disp_win_top_offset,
                &mut ret.def_disp_win_bottom_offset
            )?;
        }

        decode!(bs, &mut ret.vui_timing_info_present_flag)?;

        if ret.vui_timing_info_present_flag.0 != 0 {
            decode!(bs, &mut ret.vui_num_units_in_tick, &mut ret.vui_time_scale)?;
        }

        Ok(ret)
    }
}

impl Encode for VUIParameters {
    fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>) -> io::Result<()> {
        encode!(bs, &self.aspect_ratio_info_present_flag)?;

        if self.aspect_ratio_info_present_flag.0 != 0 {
            encode!(bs, &self.aspect_ratio_idc)?;

            if self.aspect_ratio_idc.0 == ASPECT_RATIO_IDC_EXTENDED_SAR {
                encode!(bs, &self.sar_width, &self.sar_height)?;
            }
        }

        encode!(bs, &self.overscan_info_present_flag)?;

        if self.overscan_info_present_flag.0 != 0 {
            encode!(bs, &self.overscan_appropriate_flag)?;
        }

        encode!(bs, &self.video_signal_type_present_flag)?;

        if self.video_signal_type_present_flag.0 != 0 {
            encode!(bs, &self.video_format, &self.video_full_range_flag, &self.colour_description_present_flag)?;

            if self.colour_description_present_flag.0 != 0 {
                encode!(bs, &self.colour_primaries, &self.transfer_characteristics, &self.matrix_coefficients)?;
            }
        }

        encode!(bs, &self.chroma_loc_info_present_flag)?;

        if self.chroma_loc_info_present_flag.0 != 0 {
            encode!(bs, &self.chroma_sample_loc_type_top_field, &self.chroma_sample_loc_type_bottom_field)?;
        }

        encode!(
            bs,
            &self.neutral_chroma_indication_flag,
            &self.field_seq_flag,
            &self.frame_field_info_present_flag,
            &self.default_display_window_flag
        )?;

        if self.default_display_window_flag.0 != 0 {
            encode!(
                bs,
                &self.def_disp_win_left_offset,
                &self.def_disp_win_right_offset,
                &self.def_disp_win_top_offset,
                &self.def_disp_win_bottom_offset
            )?;
        }

        encode!(bs, &self.vui_timing_info_present_flag)?;

        if self.vui_timing_info_present_flag.0 != 0 {
            encode!(bs, &self.vui_num_units_in_tick, &self.vui_time_scale)?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[allow(clippy::cognitive_complexity)]
    #[test]
    fn test_sequence_parameter_set() {
        {
            let data = vec![
                0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0xa0, 0x02, 0x80, 0x80, 0x2d, 0x16, 0x20, 0x5e, 0xe4, 0x59, 0x14,
                0xbf, 0xf2, 0xe7, 0xf1, 0x3f, 0xac, 0x05, 0xa8, 0x10, 0x10, 0x10, 0x04,
            ];
            let mut bs = Bitstream::new(data.iter().copied());

            let sps = SequenceParameterSet::decode(&mut bs).unwrap();

            assert_eq!(0, sps.sps_video_parameter_set_id.0);
            assert_eq!(0, sps.sps_max_sub_layers_minus1.0);
            assert_eq!(0x6000_0000, sps.profile_tier_level.general_profile_compatibility_flags.0);
            assert_eq!(0xb000_0000_0000, sps.profile_tier_level.general_constraint_flags.0);
            assert_eq!(0, sps.sps_seq_parameter_set_id.0);
            assert_eq!(1, sps.chroma_format_idc.0);
            assert_eq!(1280, sps.pic_width_in_luma_samples.0);
            assert_eq!(720, sps.pic_height_in_luma_samples.0);
            assert_eq!(0, sps.conformance_window_flag.0);
            assert_eq!(0, sps.bit_depth_luma_minus8.0);
            assert_eq!(0, sps.bit_depth_chroma_minus8.0);
            assert_eq!(0, sps.sps_sub_layer_ordering_info_present_flag.0);
            assert_eq!(1, sps.scaling_list_enabled_flag.0);
            assert_eq!(0, sps.sps_scaling_list_data_present_flag.0);
            assert_eq!(0, sps.amp_enabled_flag.0);
            assert_eq!(1, sps.sample_adaptive_offset_enabled_flag.0);
            assert_eq!(0, sps.pcm_enabled_flag.0);
            assert_eq!(4, sps.num_short_term_ref_pic_sets.0);
            assert_eq!(0, sps.long_term_ref_pics_present_flag.0);
            assert_eq!(1, sps.vui_parameters_present_flag.0);
            assert_eq!(0, sps.vui_parameters.vui_timing_info_present_flag.0);

            assert_eq!(bs.next_bits(1), None);

            let mut round_trip = Vec::new();
            sps.encode(&mut BitstreamWriter::new(&mut round_trip)).unwrap();
            assert_eq!(round_trip, data);
        }

        {
            let data = vec![
                0x01, 0x04, 0x08, 0x00, 0x00, 0x00, 0x9d, 0x08, 0x00, 0x00, 0x00, 0x00, 0x78, 0xb0, 0x02, 0x80, 0x80, 0x2d, 0x13, 0x65, 0x95, 0x9a, 0x49, 0x32,
                0xbc, 0x05, 0xa0, 0x20, 0x00, 0x00, 0x7d, 0x20, 0x00, 0x1d, 0x4c, 0x01,
            ];
            let mut bs = Bitstream::new(data.iter().copied());

            let sps = SequenceParameterSet::decode(&mut bs).unwrap();

            assert_eq!(0, sps.sps_video_parameter_set_id.0);
            assert_eq!(0, sps.sps_max_sub_layers_minus1.0);
            assert_eq!(0x0800_0000, sps.profile_tier_level.general_profile_compatibility_flags.0);
            assert_eq!(0x9d08_0000_0000, sps.profile_tier_level.general_constraint_flags.0);
            assert_eq!(0, sps.sps_seq_parameter_set_id.0);
            assert_eq!(2, sps.chroma_format_idc.0);
            assert_eq!(1280, sps.pic_width_in_luma_samples.0);
            assert_eq!(720, sps.pic_height_in_luma_samples.0);
            assert_eq!(0, sps.conformance_window_flag.0);
            assert_eq!(2, sps.bit_depth_luma_minus8.0);
            assert_eq!(2, sps.bit_depth_chroma_minus8.0);
            assert_eq!(1, sps.sps_sub_layer_ordering_info_present_flag.0);
            assert_eq!(0, sps.scaling_list_enabled_flag.0);
            assert_eq!(0, sps.sps_scaling_list_data_present_flag.0);
            assert_eq!(0, sps.amp_enabled_flag.0);
            assert_eq!(1, sps.sample_adaptive_offset_enabled_flag.0);
            assert_eq!(0, sps.pcm_enabled_flag.0);
            assert_eq!(0, sps.num_short_term_ref_pic_sets.0);
            assert_eq!(0, sps.long_term_ref_pics_present_flag.0);
            assert_eq!(1, sps.vui_parameters_present_flag.0);
            assert_eq!(1, sps.vui_parameters.vui_timing_info_present_flag.0);
            assert_eq!(1001, sps.vui_parameters.vui_num_units_in_tick.0);
            assert_eq!(60_000, sps.vui_parameters.vui_time_scale.0);

            assert_eq!(bs.next_bits(1), None);

            let mut round_trip = Vec::new();
            sps.encode(&mut BitstreamWriter::new(&mut round_trip)).unwrap();
            assert_eq!(round_trip, data);
        }

        {
            let data = vec![
                0x02, 0x01, 0x60, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xba, 0x00, 0x00, 0xa0, 0x00, 0xf0, 0x08, 0x00, 0x43, 0x85, 0xde, 0x49,
                0x32, 0x8c, 0x04, 0x04, 0x00, 0x00, 0x0f, 0xa4, 0x00, 0x01, 0xd4, 0xc0, 0x20,
            ];

            let mut bs = Bitstream::new(data.iter().copied());

            let sps = SequenceParameterSet::decode(&mut bs).unwrap();

            assert_eq!(0, sps.sps_video_parameter_set_id.0);
            assert_eq!(7680, sps.pic_width_in_luma_samples.0);
            assert_eq!(4320, sps.pic_height_in_luma_samples.0);
            assert_eq!(68, sps.PicHeightInCtbsY());
            assert_eq!(120, sps.PicWidthInCtbsY());

            assert_eq!(bs.next_bits(1), None);

            let mut round_trip = Vec::new();
            sps.encode(&mut BitstreamWriter::new(&mut round_trip)).unwrap();
            assert_eq!(round_trip, data);
        }
    }
}
