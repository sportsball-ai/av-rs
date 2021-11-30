use super::{
    decode, encode, nal_unit::*, syntax_elements::*, Bitstream, BitstreamWriter, Decode, Encode, PictureParameterSet, SequenceParameterSet, ShortTermRefPicSet,
};
use std::io;

pub const SLICE_TYPE_B: u64 = 0;
pub const SLICE_TYPE_P: u64 = 1;
pub const SLICE_TYPE_I: u64 = 2;

#[derive(Debug, Default)]
pub struct RefPicListsModification {
    pub ref_pic_list_modification_flag_l0: U1,

    //if( ref_pic_list_modification_flag_l0 )
    //for( i = 0; i <= num_ref_idx_l0_active_minus1; i++ )
    pub list_entry_l0: Vec<u64>,

    //if( slice_type = = B ) {
    pub ref_pic_list_modification_flag_l1: U1,
    //if( ref_pic_list_modification_flag_l1 )
    //for( i = 0; i <= num_ref_idx_l1_active_minus1; i++ )
    pub list_entry_l1: Vec<u64>,
    //}
}

fn ceil_log2(n: u64) -> u32 {
    n.next_power_of_two().trailing_zeros()
}

impl RefPicListsModification {
    pub fn decode<T: Iterator<Item = u8>>(
        bs: &mut Bitstream<T>,
        slice_type: u64,
        #[allow(non_snake_case)] NumPicTotalCurr: u64,
        num_ref_idx_l0_active_minus1: u64,
        num_ref_idx_l1_active_minus1: u64,
    ) -> io::Result<Self> {
        let mut ret = Self::default();

        bs.decode(&mut ret.ref_pic_list_modification_flag_l0)?;

        if ret.ref_pic_list_modification_flag_l0.0 != 0 {
            for _ in 0..=num_ref_idx_l0_active_minus1 {
                ret.list_entry_l0.push(bs.read_bits(ceil_log2(NumPicTotalCurr) as _)?);
            }
        }

        if slice_type == SLICE_TYPE_B {
            bs.decode(&mut ret.ref_pic_list_modification_flag_l1)?;
            if ret.ref_pic_list_modification_flag_l1.0 != 0 {
                for _ in 0..=num_ref_idx_l1_active_minus1 {
                    ret.list_entry_l1.push(bs.read_bits(ceil_log2(NumPicTotalCurr) as _)?);
                }
            }
        }

        Ok(ret)
    }

    pub fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>, slice_type: u64, #[allow(non_snake_case)] NumPicTotalCurr: u64) -> io::Result<()> {
        bs.encode(&self.ref_pic_list_modification_flag_l0)?;

        if self.ref_pic_list_modification_flag_l0.0 != 0 {
            for entry in &self.list_entry_l0 {
                bs.write_bits(*entry, ceil_log2(NumPicTotalCurr) as _)?;
            }
        }

        if slice_type == SLICE_TYPE_B {
            bs.encode(&self.ref_pic_list_modification_flag_l1)?;
            if self.ref_pic_list_modification_flag_l1.0 != 0 {
                for entry in &self.list_entry_l1 {
                    bs.write_bits(*entry, ceil_log2(NumPicTotalCurr) as _)?;
                }
            }
        }

        Ok(())
    }
}

#[derive(Debug, Default)]
pub struct SliceSegmentHeader {
    pub first_slice_segment_in_pic_flag: U1,

    //if( nal_unit_type >= BLA_W_LP && nal_unit_type <= RSV_IRAP_VCL23 )
    pub no_output_of_prior_pics_flag: U1,

    pub slice_pic_parameter_set_id: UE,

    //if( !first_slice_segment_in_pic_flag ) {
    //if( dependent_slice_segments_enabled_flag )
    pub dependent_slice_segment_flag: U1,
    pub slice_segment_address: u64,
    //}

    //if( !dependent_slice_segment_flag ) {

    //for( i = 0; i < num_extra_slice_header_bits; i++ )
    pub slice_reserved_flag: Vec<U1>,

    pub slice_type: UE,

    //if( output_flag_present_flag )
    pub pic_output_flag: U1,

    //if( separate_colour_plane_flag = = 1 )
    pub colour_plane_id: U2,

    //if( nal_unit_type != IDR_W_RADL && nal_unit_type != IDR_N_LP ) {
    pub slice_pic_order_cnt_lsb: u64,
    pub short_term_ref_pic_set_sps_flag: U1,

    //if( !short_term_ref_pic_set_sps_flag )
    pub st_ref_pic_set: ShortTermRefPicSet,
    //else if( num_short_term_ref_pic_sets > 1 )
    pub short_term_ref_pic_set_idx: u64,

    //if( long_term_ref_pics_present_flag ) {
    //if( num_long_term_ref_pics_sps > 0 )
    pub num_long_term_sps: UE,
    pub num_long_term_pics: UE,
    //for( i = 0; i < num_long_term_sps + num_long_term_pics; i++ ) {
    //if( i < num_long_term_sps ) {
    //if( num_long_term_ref_pics_sps > 1 )
    pub lt_idx_sps: Vec<u64>,
    //} else {
    pub poc_lsb_lt: Vec<u64>,
    pub used_by_curr_pic_lt_flag: Vec<U1>,
    //}
    pub delta_poc_msb_present_flag: Vec<U1>,
    //if( delta_poc_msb_present_flag[ i ] )
    pub delta_poc_msb_cycle_lt: Vec<UE>,
    //}
    //}
    //if( sps_temporal_mvp_enabled_flag )
    pub slice_temporal_mvp_enabled_flag: U1,
    //}
    //if( sample_adaptive_offset_enabled_flag ) {
    pub slice_sao_luma_flag: U1,
    //if( ChromaArrayType != 0 )
    pub slice_sao_chroma_flag: U1,
    //}
    //if(slice_type == P || slice_type == B){
    pub num_ref_idx_active_override_flag: U1,
    //if( num_ref_idx_active_override_flag ) {
    pub num_ref_idx_l0_active_minus1: UE,
    //if( slice_type = = B )
    pub num_ref_idx_l1_active_minus1: UE,
    //}

    //if( lists_modification_present_flag && NumPicTotalCurr > 1 )
    pub ref_pic_lists_modification: RefPicListsModification,

    //if( slice_type = = B )
    pub mvd_l1_zero_flag: U1,

    //if( cabac_init_present_flag )
    pub cabac_init_flag: U1,

    //if( slice_temporal_mvp_enabled_flag ) {
    //if( slice_type = = B )
    pub collocated_from_l0_flag: U1,
    //if( ( collocated_from_l0_flag && num_ref_idx_l0_active_minus1 > 0 ) || ( !collocated_from_l0_flag && num_ref_idx_l1_active_minus1 > 0 ) )
    pub collocated_ref_idx: UE,
    //}
    pub five_minus_max_num_merge_cand: UE,

    pub slice_qp_delta: SE,
    //if( pps_slice_chroma_qp_offsets_present_flag ) {
    pub slice_cb_qp_offset: SE,
    pub slice_cr_qp_offset: SE,
    //}

    //if( deblocking_filter_override_enabled_flag )
    pub deblocking_filter_override_flag: U1,
    //if( deblocking_filter_override_flag ) {
    pub slice_deblocking_filter_disabled_flag: U1,
    //if( !slice_deblocking_filter_disabled_flag ) {
    pub slice_beta_offset_div2: SE,
    pub slice_tc_offset_div2: SE,
    //}
    //}
    //if( pps_loop_filter_across_slices_enabled_flag && ( slice_sao_luma_flag || slice_sao_chroma_flag || !slice_deblocking_filter_disabled_flag ) )
    pub slice_loop_filter_across_slices_enabled_flag: U1,
    //}

    //if( tiles_enabled_flag || entropy_coding_sync_enabled_flag ) {
    pub num_entry_point_offsets: UE,
    //if( num_entry_point_offsets > 0 ) {
    pub offset_len_minus1: UE,
    //for( i = 0; i < num_entry_point_offsets; i++ )
    pub entry_point_offset_minus1: Vec<u64>,
    //}
    //}

    //if( slice_segment_header_extension_present_flag ) {
    pub slice_segment_header_extension_length: UE,
    //for( i = 0; i < slice_segment_header_extension_length; i++)
    pub slice_segment_header_extension_data_byte: Vec<U8>,
    //}
}

#[allow(non_snake_case)]
impl SliceSegmentHeader {
    // TODO: pps should probably be a map so we can find the correct pps based on slice_pic_parameter_set_id
    #[allow(clippy::cognitive_complexity)]
    pub fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>, nal_unit_type: u8, sps: &SequenceParameterSet, pps: &PictureParameterSet) -> io::Result<Self> {
        if pps.pps_range_extension_flag.0 != 0 {
            return Err(io::Error::new(io::ErrorKind::Other, "the pps range extension is not supported"));
        }
        if pps.pps_scc_extension_flag.0 != 0 {
            return Err(io::Error::new(io::ErrorKind::Other, "the pps scc extension is not supported"));
        }

        let mut ret = Self::default();

        bs.decode(&mut ret.first_slice_segment_in_pic_flag)?;

        if (NAL_UNIT_TYPE_BLA_W_LP..=NAL_UNIT_TYPE_RSV_IRAP_VCL23).contains(&nal_unit_type) {
            bs.decode(&mut ret.no_output_of_prior_pics_flag)?;
        }

        bs.decode(&mut ret.slice_pic_parameter_set_id)?;

        if ret.first_slice_segment_in_pic_flag.0 == 0 {
            if pps.dependent_slice_segments_enabled_flag.0 != 0 {
                bs.decode(&mut ret.dependent_slice_segment_flag)?;
            }
            ret.slice_segment_address = bs.read_bits(ceil_log2(sps.PicSizeInCtbsY()) as _)?;
        }

        ret.collocated_from_l0_flag.0 = 1;

        if ret.dependent_slice_segment_flag.0 == 0 {
            for _ in 0..pps.num_extra_slice_header_bits.0 {
                ret.slice_reserved_flag.push(U1::decode(bs)?);
            }

            bs.decode(&mut ret.slice_type)?;

            if pps.output_flag_present_flag.0 != 0 {
                bs.decode(&mut ret.pic_output_flag)?;
            }

            if sps.separate_colour_plane_flag.0 == 1 {
                bs.decode(&mut ret.colour_plane_id)?;
            }

            if nal_unit_type != NAL_UNIT_TYPE_IDR_W_RADL && nal_unit_type != NAL_UNIT_TYPE_IDR_N_LP {
                ret.slice_pic_order_cnt_lsb = bs.read_bits(sps.log2_max_pic_order_cnt_lsb_minus4.0 as usize + 4)?;
                bs.decode(&mut ret.short_term_ref_pic_set_sps_flag)?;

                if ret.short_term_ref_pic_set_sps_flag.0 == 0 {
                    ret.st_ref_pic_set = ShortTermRefPicSet::decode(bs, sps.num_short_term_ref_pic_sets.0)?;
                } else if sps.num_short_term_ref_pic_sets.0 > 1 {
                    ret.short_term_ref_pic_set_idx = bs.read_bits(ceil_log2(sps.num_short_term_ref_pic_sets.0) as _)?;
                }

                if sps.long_term_ref_pics_present_flag.0 != 0 {
                    if sps.num_long_term_ref_pics_sps.0 > 0 {
                        bs.decode(&mut ret.num_long_term_sps)?;
                    }
                    bs.decode(&mut ret.num_long_term_pics)?;

                    let n = (ret.num_long_term_sps.0 + ret.num_long_term_pics.0) as usize;
                    ret.lt_idx_sps.resize(n, 0);
                    ret.poc_lsb_lt.resize(n, 0);
                    ret.used_by_curr_pic_lt_flag.resize(n, U1(0));
                    ret.delta_poc_msb_cycle_lt.resize(n, UE(0));
                    for i in 0..n {
                        if i < ret.num_long_term_sps.0 as _ {
                            if sps.num_long_term_ref_pics_sps.0 > 1 {
                                ret.lt_idx_sps[i] = bs.read_bits(ceil_log2(sps.num_long_term_ref_pics_sps.0) as _)?;
                            }
                        } else {
                            ret.poc_lsb_lt[i] = bs.read_bits(sps.log2_max_pic_order_cnt_lsb_minus4.0 as usize + 4)?;
                            ret.used_by_curr_pic_lt_flag[i] = U1::decode(bs)?;
                        }
                        ret.delta_poc_msb_present_flag.push(U1::decode(bs)?);
                        if ret.delta_poc_msb_present_flag[i].0 != 0 {
                            ret.delta_poc_msb_cycle_lt[i] = UE::decode(bs)?;
                        }
                    }
                }

                if sps.sps_temporal_mvp_enabled_flag.0 != 0 {
                    bs.decode(&mut ret.slice_temporal_mvp_enabled_flag)?;
                }
            }

            if sps.sample_adaptive_offset_enabled_flag.0 != 0 {
                bs.decode(&mut ret.slice_sao_luma_flag)?;
                if sps.ChromaArrayType() != 0 {
                    bs.decode(&mut ret.slice_sao_chroma_flag)?;
                }
            }

            if ret.slice_type.0 == SLICE_TYPE_P || ret.slice_type.0 == SLICE_TYPE_B {
                bs.decode(&mut ret.num_ref_idx_active_override_flag)?;
                if ret.num_ref_idx_active_override_flag.0 != 0 {
                    bs.decode(&mut ret.num_ref_idx_l0_active_minus1)?;
                    if ret.slice_type.0 == SLICE_TYPE_B {
                        bs.decode(&mut ret.num_ref_idx_l1_active_minus1)?;
                    }
                }

                let NumPicTotalCurr = ret.NumPicTotalCurr(sps);
                if pps.lists_modification_present_flag.0 != 0 && NumPicTotalCurr > 1 {
                    ret.ref_pic_lists_modification = RefPicListsModification::decode(
                        bs,
                        ret.slice_type.0,
                        NumPicTotalCurr,
                        ret.num_ref_idx_l0_active_minus1.0,
                        ret.num_ref_idx_l1_active_minus1.0,
                    )?;
                }

                if ret.slice_type.0 == SLICE_TYPE_B {
                    bs.decode(&mut ret.mvd_l1_zero_flag)?;
                }

                if pps.cabac_init_present_flag.0 != 0 {
                    bs.decode(&mut ret.cabac_init_flag)?;
                }

                if ret.slice_temporal_mvp_enabled_flag.0 != 0 {
                    if ret.slice_type.0 == SLICE_TYPE_B {
                        bs.decode(&mut ret.collocated_from_l0_flag)?;
                    }
                    if (ret.collocated_from_l0_flag.0 != 0 && ret.num_ref_idx_l0_active_minus1.0 > 0)
                        || (ret.collocated_from_l0_flag.0 == 0 && ret.num_ref_idx_l1_active_minus1.0 > 0)
                    {
                        bs.decode(&mut ret.collocated_ref_idx)?;
                    }
                }

                if (pps.weighted_pred_flag.0 != 0 && ret.slice_type.0 == SLICE_TYPE_P) || (pps.weighted_bipred_flag.0 != 0 && ret.slice_type.0 == SLICE_TYPE_B)
                {
                    return Err(io::Error::new(io::ErrorKind::Other, "prediction weight tables are not supported"));
                }

                bs.decode(&mut ret.five_minus_max_num_merge_cand)?;
            }

            bs.decode(&mut ret.slice_qp_delta)?;

            if pps.pps_slice_chroma_qp_offsets_present_flag.0 != 0 {
                decode!(bs, &mut ret.slice_cb_qp_offset, &mut ret.slice_cr_qp_offset)?;
            }

            if pps.deblocking_filter_override_enabled_flag.0 != 0 {
                bs.decode(&mut ret.deblocking_filter_override_flag)?;
            }

            if ret.deblocking_filter_override_flag.0 != 0 {
                bs.decode(&mut ret.slice_deblocking_filter_disabled_flag)?;
                if ret.slice_deblocking_filter_disabled_flag.0 == 0 {
                    decode!(bs, &mut ret.slice_beta_offset_div2, &mut ret.slice_tc_offset_div2)?;
                }
            }

            if pps.pps_loop_filter_across_slices_enabled_flag.0 != 0
                && (ret.slice_sao_luma_flag.0 != 0 || ret.slice_sao_chroma_flag.0 != 0 || ret.slice_deblocking_filter_disabled_flag.0 == 0)
            {
                bs.decode(&mut ret.slice_loop_filter_across_slices_enabled_flag)?;
            }
        }

        if pps.tiles_enabled_flag.0 != 0 || pps.entropy_coding_sync_enabled_flag.0 != 0 {
            bs.decode(&mut ret.num_entry_point_offsets)?;
            if ret.num_entry_point_offsets.0 > 0 {
                bs.decode(&mut ret.offset_len_minus1)?;
                for _ in 0..ret.num_entry_point_offsets.0 {
                    ret.entry_point_offset_minus1.push(bs.read_bits(ret.offset_len_minus1.0 as usize + 1)?);
                }
            }
        }

        if pps.slice_segment_header_extension_present_flag.0 != 0 {
            bs.decode(&mut ret.slice_segment_header_extension_length)?;
            for _ in 0..ret.slice_segment_header_extension_length.0 {
                ret.slice_segment_header_extension_data_byte.push(U8::decode(bs)?);
            }
        }

        ByteAlignment::decode(bs)?;

        Ok(ret)
    }

    // TODO: pps should probably be a map so we can find the correct pps based on slice_pic_parameter_set_id
    #[allow(clippy::cognitive_complexity)]
    pub fn encode<T: io::Write>(
        &self,
        bs: &mut BitstreamWriter<T>,
        nal_unit_type: u8,
        sps: &SequenceParameterSet,
        pps: &PictureParameterSet,
    ) -> io::Result<()> {
        if pps.pps_range_extension_flag.0 != 0 {
            return Err(io::Error::new(io::ErrorKind::Other, "the pps range extension is not supported"));
        }
        if pps.pps_scc_extension_flag.0 != 0 {
            return Err(io::Error::new(io::ErrorKind::Other, "the pps scc extension is not supported"));
        }

        bs.encode(&self.first_slice_segment_in_pic_flag)?;

        if (NAL_UNIT_TYPE_BLA_W_LP..=NAL_UNIT_TYPE_RSV_IRAP_VCL23).contains(&nal_unit_type) {
            bs.encode(&self.no_output_of_prior_pics_flag)?;
        }

        bs.encode(&self.slice_pic_parameter_set_id)?;

        if self.first_slice_segment_in_pic_flag.0 == 0 {
            if pps.dependent_slice_segments_enabled_flag.0 != 0 {
                bs.encode(&self.dependent_slice_segment_flag)?;
            }
            bs.write_bits(self.slice_segment_address, ceil_log2(sps.PicSizeInCtbsY()) as _)?;
        }

        if self.dependent_slice_segment_flag.0 == 0 {
            bs.encode(&self.slice_reserved_flag)?;

            bs.encode(&self.slice_type)?;

            if pps.output_flag_present_flag.0 != 0 {
                bs.encode(&self.pic_output_flag)?;
            }

            if sps.separate_colour_plane_flag.0 == 1 {
                bs.encode(&self.colour_plane_id)?;
            }

            if nal_unit_type != NAL_UNIT_TYPE_IDR_W_RADL && nal_unit_type != NAL_UNIT_TYPE_IDR_N_LP {
                bs.write_bits(self.slice_pic_order_cnt_lsb, sps.log2_max_pic_order_cnt_lsb_minus4.0 as usize + 4)?;
                bs.encode(&self.short_term_ref_pic_set_sps_flag)?;

                if self.short_term_ref_pic_set_sps_flag.0 == 0 {
                    self.st_ref_pic_set.encode(bs, sps.num_short_term_ref_pic_sets.0)?;
                } else if sps.num_short_term_ref_pic_sets.0 > 1 {
                    bs.write_bits(self.short_term_ref_pic_set_idx, ceil_log2(sps.num_short_term_ref_pic_sets.0) as _)?;
                }

                if sps.long_term_ref_pics_present_flag.0 != 0 {
                    if sps.num_long_term_ref_pics_sps.0 > 0 {
                        bs.encode(&self.num_long_term_sps)?;
                    }
                    bs.encode(&self.num_long_term_pics)?;

                    let n = (self.num_long_term_sps.0 + self.num_long_term_pics.0) as usize;
                    for i in 0..n {
                        if i < self.num_long_term_sps.0 as _ {
                            if sps.num_long_term_ref_pics_sps.0 > 1 {
                                bs.write_bits(self.lt_idx_sps[i], ceil_log2(sps.num_long_term_ref_pics_sps.0) as _)?;
                            }
                        } else {
                            bs.write_bits(self.poc_lsb_lt[i], sps.log2_max_pic_order_cnt_lsb_minus4.0 as usize + 4)?;
                            self.used_by_curr_pic_lt_flag[i].encode(bs)?;
                        }
                        self.delta_poc_msb_present_flag.encode(bs)?;
                        if self.delta_poc_msb_present_flag[i].0 != 0 {
                            self.delta_poc_msb_cycle_lt[i].encode(bs)?;
                        }
                    }
                }

                if sps.sps_temporal_mvp_enabled_flag.0 != 0 {
                    bs.encode(&self.slice_temporal_mvp_enabled_flag)?;
                }
            }

            if sps.sample_adaptive_offset_enabled_flag.0 != 0 {
                bs.encode(&self.slice_sao_luma_flag)?;
                if sps.ChromaArrayType() != 0 {
                    bs.encode(&self.slice_sao_chroma_flag)?;
                }
            }

            if self.slice_type.0 == SLICE_TYPE_P || self.slice_type.0 == SLICE_TYPE_B {
                bs.encode(&self.num_ref_idx_active_override_flag)?;
                if self.num_ref_idx_active_override_flag.0 != 0 {
                    bs.encode(&self.num_ref_idx_l0_active_minus1)?;
                    if self.slice_type.0 == SLICE_TYPE_B {
                        bs.encode(&self.num_ref_idx_l1_active_minus1)?;
                    }
                }

                let NumPicTotalCurr = self.NumPicTotalCurr(sps);
                if pps.lists_modification_present_flag.0 != 0 && NumPicTotalCurr > 1 {
                    self.ref_pic_lists_modification.encode(bs, self.slice_type.0, NumPicTotalCurr)?;
                }

                if self.slice_type.0 == SLICE_TYPE_B {
                    bs.encode(&self.mvd_l1_zero_flag)?;
                }

                if pps.cabac_init_present_flag.0 != 0 {
                    bs.encode(&self.cabac_init_flag)?;
                }

                if self.slice_temporal_mvp_enabled_flag.0 != 0 {
                    if self.slice_type.0 == SLICE_TYPE_B {
                        bs.encode(&self.collocated_from_l0_flag)?;
                    }
                    if (self.collocated_from_l0_flag.0 != 0 && self.num_ref_idx_l0_active_minus1.0 > 0)
                        || (self.collocated_from_l0_flag.0 == 0 && self.num_ref_idx_l1_active_minus1.0 > 0)
                    {
                        bs.encode(&self.collocated_ref_idx)?;
                    }
                }

                if (pps.weighted_pred_flag.0 != 0 && self.slice_type.0 == SLICE_TYPE_P)
                    || (pps.weighted_bipred_flag.0 != 0 && self.slice_type.0 == SLICE_TYPE_B)
                {
                    return Err(io::Error::new(io::ErrorKind::Other, "prediction weight tables are not supported"));
                }

                bs.encode(&self.five_minus_max_num_merge_cand)?;
            }

            bs.encode(&self.slice_qp_delta)?;

            if pps.pps_slice_chroma_qp_offsets_present_flag.0 != 0 {
                encode!(bs, &self.slice_cb_qp_offset, &self.slice_cr_qp_offset)?;
            }

            if pps.deblocking_filter_override_enabled_flag.0 != 0 {
                bs.encode(&self.deblocking_filter_override_flag)?;
            }

            if self.deblocking_filter_override_flag.0 != 0 {
                bs.encode(&self.slice_deblocking_filter_disabled_flag)?;
                if self.slice_deblocking_filter_disabled_flag.0 == 0 {
                    encode!(bs, &self.slice_beta_offset_div2, &self.slice_tc_offset_div2)?;
                }
            }

            if pps.pps_loop_filter_across_slices_enabled_flag.0 != 0
                && (self.slice_sao_luma_flag.0 != 0 || self.slice_sao_chroma_flag.0 != 0 || self.slice_deblocking_filter_disabled_flag.0 == 0)
            {
                bs.encode(&self.slice_loop_filter_across_slices_enabled_flag)?;
            }
        }

        if pps.tiles_enabled_flag.0 != 0 || pps.entropy_coding_sync_enabled_flag.0 != 0 {
            bs.encode(&self.num_entry_point_offsets)?;
            if self.num_entry_point_offsets.0 > 0 {
                bs.encode(&self.offset_len_minus1)?;
                for i in 0..self.num_entry_point_offsets.0 {
                    bs.write_bits(self.entry_point_offset_minus1[i as usize], self.offset_len_minus1.0 as usize + 1)?;
                }
            }
        }

        if pps.slice_segment_header_extension_present_flag.0 != 0 {
            bs.encode(&self.slice_segment_header_extension_length)?;
            for i in 0..self.slice_segment_header_extension_length.0 {
                self.slice_segment_header_extension_data_byte[i as usize].encode(bs)?;
            }
        }

        ByteAlignment.encode(bs)?;

        Ok(())
    }

    pub fn UsedByCurrPicLt(&self, i: usize, sps: &SequenceParameterSet) -> bool {
        if i < self.num_long_term_sps.0 as _ {
            sps.used_by_curr_pic_lt_sps_flag[self.lt_idx_sps[i] as usize].0 != 0
        } else {
            self.used_by_curr_pic_lt_flag[i].0 != 0
        }
    }

    pub fn CurrRpsIdx(&self, sps: &SequenceParameterSet) -> usize {
        match self.short_term_ref_pic_set_sps_flag.0 {
            1 => self.short_term_ref_pic_set_idx as usize,
            _ => sps.num_short_term_ref_pic_sets.0 as usize,
        }
    }

    pub fn st_ref_pic_set<'a>(&'a self, sps: &'a SequenceParameterSet) -> &'a ShortTermRefPicSet {
        match self.short_term_ref_pic_set_sps_flag.0 {
            1 => &sps.st_ref_pic_set[self.short_term_ref_pic_set_idx as usize],
            _ => &self.st_ref_pic_set,
        }
    }

    pub fn NumPicTotalCurr(&self, sps: &SequenceParameterSet) -> u64 {
        let mut ret = 0;
        let CurrRpsIdx = self.CurrRpsIdx(sps);
        let st_rps = self.st_ref_pic_set(sps);
        for i in 0..st_rps.NumNegativePics(CurrRpsIdx) {
            if st_rps.UsedByCurrPicS0(CurrRpsIdx, i as _) {
                ret += 1;
            }
        }
        for i in 0..st_rps.NumPositivePics(CurrRpsIdx) {
            if st_rps.UsedByCurrPicS1(CurrRpsIdx, i as _) {
                ret += 1;
            }
        }
        for i in 0..(self.num_long_term_sps.0 + self.num_long_term_pics.0) {
            if self.UsedByCurrPicLt(i as _, sps) {
                ret += 1;
            }
            // TODO: support pps_scc_extension
        }
        ret
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_ceil_log2() {
        assert_eq!(ceil_log2(1), 0);
        assert_eq!(ceil_log2(2), 1);
        assert_eq!(ceil_log2(3), 2);
        assert_eq!(ceil_log2(4), 2);
    }

    #[test]
    fn test_slice_segment_header() {
        {
            let sps_data = vec![
                0x02, 0x01, 0x60, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xba, 0x00, 0x00, 0xa0, 0x00, 0xf0, 0x08, 0x00, 0x43, 0x85, 0xde, 0x49,
                0x32, 0x8c, 0x04, 0x04, 0x00, 0x00, 0x0f, 0xa4, 0x00, 0x01, 0xd4, 0xc0, 0x20,
            ];
            let sps = SequenceParameterSet::decode(&mut Bitstream::new(sps_data)).unwrap();

            let pps_data = vec![0xc1, 0x62, 0x4f, 0x08, 0x20, 0x26, 0x4c, 0x90];
            let pps = PictureParameterSet::decode(&mut Bitstream::new(pps_data)).unwrap();

            let data = vec![
                0xd0, 0x97, 0xfa, 0x01, 0x20, 0x34, 0x2b, 0x82, 0x0d, 0x13, 0x80, 0x69, 0x46, 0x92, 0x20, 0xb0, 0xc0, 0x86, 0xec, 0x4f, 0xe1, 0x82, 0x08, 0x19,
                0x36, 0x42, 0x1a, 0x2c, 0xb0, 0xe8, 0x06, 0x60, 0x31, 0xa1, 0xac, 0x23, 0xb9, 0x91, 0x05, 0x8c, 0x28, 0xf1, 0x5d, 0x10, 0x60, 0x58, 0x62, 0x51,
                0x0d, 0xd9, 0x1f, 0x49, 0x4c, 0x20, 0x20, 0x9c, 0x85, 0xb0, 0x2f, 0x02, 0xe9, 0x1d, 0x4a, 0x16, 0x01, 0x96, 0x0c, 0xb0, 0x51, 0x15, 0xe8, 0x0f,
                0xe0, 0x93, 0x04, 0x21, 0xc5, 0x06, 0x62, 0x25, 0x50, 0x91, 0x85, 0xe8, 0x3f, 0x63, 0xe7, 0x66, 0x03, 0xba, 0xc1, 0x22, 0x46, 0xc1, 0xa3, 0x0f,
                0x20, 0x18, 0x40, 0xb2, 0x05, 0x11, 0xe8, 0xeb, 0xac, 0x64, 0x20, 0xc1, 0x07, 0xe0, 0x14, 0xe1, 0xb2, 0x5e, 0x20, 0x68, 0x41, 0x4a, 0x0d, 0x70,
                0x3e, 0x82, 0xe8, 0x16, 0x60, 0xbc, 0x06, 0x60, 0x3c, 0x0b, 0xf4, 0x4f, 0xf0, 0xbc, 0x88, 0xa4, 0x0f, 0xe1, 0xfa, 0x04, 0x08, 0x24, 0x01, 0x0e,
                0x0d, 0xc0, 0x50, 0x03, 0x24, 0x1a, 0x00, 0xd7, 0x05, 0xa8, 0x35, 0x01, 0xfc, 0x2b, 0x80, 0xc9, 0x0d, 0x78, 0x0b, 0xa0, 0xdf, 0x03, 0x58, 0x1c,
                0x41, 0x14, 0x0c, 0x20, 0x39, 0x03, 0x84, 0x1c, 0x40, 0xc5, 0x06, 0x28, 0x2e, 0xc1, 0x4e, 0x0b, 0x50, 0x63, 0x02, 0x18, 0x18, 0x81, 0x39, 0x0b,
                0xc0, 0x48, 0x42, 0x62, 0x16, 0x50, 0x8d, 0x86, 0x98, 0x24, 0x21, 0x4d, 0x0b, 0xc8, 0x69, 0x82, 0x7a, 0x17, 0xc0, 0x6e, 0x03, 0xc0, 0x13, 0x60,
                0xc4, 0x08, 0xe8, 0x35, 0x01, 0xa0, 0x13, 0x40, 0x72, 0x05, 0x18, 0x21, 0x61, 0x85, 0x0a, 0x28, 0x4a, 0xc1, 0xd8, 0x11, 0xf0, 0x46, 0xc0,
            ];
            let mut bs = Bitstream::new(data.iter().copied());
            let ssh = SliceSegmentHeader::decode(&mut bs, 1, &sps, &pps).unwrap();

            assert_eq!(ssh.num_entry_point_offsets.0, 143);

            assert_eq!(bs.next_bits(1), None);

            let mut round_trip = Vec::new();
            ssh.encode(&mut BitstreamWriter::new(&mut round_trip), 1, &sps, &pps).unwrap();
            assert_eq!(round_trip, data);
        }
    }

    #[test]
    fn test_slice_segment_header_2() {
        {
            let sps_data = vec![
                0x01, 0x02, 0x60, 0x00, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb4, 0xa0, 0x00, 0xf0, 0x08, 0x00, 0x43, 0x84, 0xd8, 0xdb, 0xe4, 0x91,
                0x4b, 0xd3, 0x50, 0x10, 0x10, 0x10, 0x08,
            ];
            let sps = SequenceParameterSet::decode(&mut Bitstream::new(sps_data)).unwrap();

            let pps_data = vec![0xc0, 0xf2, 0xc6, 0x8d, 0x09, 0xc0, 0xa0, 0x14, 0x7b, 0x24];
            let pps = PictureParameterSet::decode(&mut Bitstream::new(pps_data)).unwrap();

            let data = vec![0xd0, 0x00, 0x11, 0x74, 0x00, 0x01, 0x7a, 0x48, 0x36, 0xf8];
            let mut bs = Bitstream::new(data.iter().copied());
            let ssh = SliceSegmentHeader::decode(&mut bs, 1, &sps, &pps).unwrap();

            assert_eq!(bs.next_bits(1), None);

            let mut round_trip = Vec::new();
            ssh.encode(&mut BitstreamWriter::new(&mut round_trip), 1, &sps, &pps).unwrap();
            assert_eq!(round_trip, data);
        }
    }
}
