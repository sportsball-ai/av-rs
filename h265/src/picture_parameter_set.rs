use super::{decode, encode, syntax_elements::*, Bitstream, BitstreamWriter, Decode, Encode};
use std::io;

// ITU-T H.265, 11/2019 7.3.2.3.1
#[derive(Debug, Default)]
pub struct PictureParameterSet {
    pub pps_pic_parameter_set_id: UE,
    pub pps_seq_parameter_set_id: UE,
    pub dependent_slice_segments_enabled_flag: U1,
    pub output_flag_present_flag: U1,
    pub num_extra_slice_header_bits: U3,
    pub sign_data_hiding_enabled_flag: U1,
    pub cabac_init_present_flag: U1,
    pub num_ref_idx_l0_default_active_minus1: UE,
    pub num_ref_idx_l1_default_active_minus1: UE,
    pub init_qp_minus26: SE,
    pub constrained_intra_pred_flag: U1,
    pub transform_skip_enabled_flag: U1,
    pub cu_qp_delta_enabled_flag: U1,

    // if( cu_qp_delta_enabled_flag )
    pub diff_cu_qp_delta_depth: UE,

    pub pps_cb_qp_offset: SE,
    pub pps_cr_qp_offset: SE,
    pub pps_slice_chroma_qp_offsets_present_flag: U1,
    pub weighted_pred_flag: U1,
    pub weighted_bipred_flag: U1,
    pub transquant_bypass_enabled_flag: U1,
    pub tiles_enabled_flag: U1,
    pub entropy_coding_sync_enabled_flag: U1,

    // if( tiles_enabled_flag ) {
    pub num_tile_columns_minus1: UE,
    pub num_tile_rows_minus1: UE,
    pub uniform_spacing_flag: U1,
    //   if( !uniform_spacing_flag ) {
    //     for( i = 0; i < num_tile_columns_minus1; i++ )
    pub column_width_minus1: Vec<UE>,
    //     for( i = 0; i < num_tile_rows_minus1; i++ )
    pub row_height_minus1: Vec<UE>,
    //   }
    pub loop_filter_across_tiles_enabled_flag: U1,
    // }

    // the remaining bits whose fields we don't currently support parsing
    pub remaining_bits: Vec<U1>,
}

impl Decode for PictureParameterSet {
    fn decode<'a, T: Iterator<Item = &'a u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(
            bs,
            &mut ret.pps_pic_parameter_set_id,
            &mut ret.pps_seq_parameter_set_id,
            &mut ret.dependent_slice_segments_enabled_flag,
            &mut ret.output_flag_present_flag,
            &mut ret.num_extra_slice_header_bits,
            &mut ret.sign_data_hiding_enabled_flag,
            &mut ret.cabac_init_present_flag,
            &mut ret.num_ref_idx_l0_default_active_minus1,
            &mut ret.num_ref_idx_l1_default_active_minus1,
            &mut ret.init_qp_minus26,
            &mut ret.constrained_intra_pred_flag,
            &mut ret.transform_skip_enabled_flag,
            &mut ret.cu_qp_delta_enabled_flag
        )?;

        if ret.cu_qp_delta_enabled_flag.0 != 0 {
            decode!(bs, &mut ret.diff_cu_qp_delta_depth)?;
        }

        decode!(
            bs,
            &mut ret.pps_cb_qp_offset,
            &mut ret.pps_cr_qp_offset,
            &mut ret.pps_slice_chroma_qp_offsets_present_flag,
            &mut ret.weighted_pred_flag,
            &mut ret.weighted_bipred_flag,
            &mut ret.transquant_bypass_enabled_flag,
            &mut ret.tiles_enabled_flag,
            &mut ret.entropy_coding_sync_enabled_flag
        )?;

        if ret.tiles_enabled_flag.0 != 0 {
            decode!(
                bs,
                &mut ret.num_tile_columns_minus1,
                &mut ret.num_tile_rows_minus1,
                &mut ret.uniform_spacing_flag
            )?;
            if ret.uniform_spacing_flag.0 != 0 {
                for _ in 0..ret.num_tile_columns_minus1.0 {
                    ret.column_width_minus1.push(UE::decode(bs)?);
                }
                for _ in 0..ret.num_tile_rows_minus1.0 {
                    ret.row_height_minus1.push(UE::decode(bs)?);
                }
            }
            decode!(bs, &mut ret.loop_filter_across_tiles_enabled_flag)?;
        }

        ret.remaining_bits = bs.bits().map(|b| U1(b as _)).collect();

        Ok(ret)
    }
}

impl Encode for PictureParameterSet {
    fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>) -> io::Result<()> {
        encode!(
            bs,
            &self.pps_pic_parameter_set_id,
            &self.pps_seq_parameter_set_id,
            &self.dependent_slice_segments_enabled_flag,
            &self.output_flag_present_flag,
            &self.num_extra_slice_header_bits,
            &self.sign_data_hiding_enabled_flag,
            &self.cabac_init_present_flag,
            &self.num_ref_idx_l0_default_active_minus1,
            &self.num_ref_idx_l1_default_active_minus1,
            &self.init_qp_minus26,
            &self.constrained_intra_pred_flag,
            &self.transform_skip_enabled_flag,
            &self.cu_qp_delta_enabled_flag
        )?;

        if self.cu_qp_delta_enabled_flag.0 != 0 {
            encode!(bs, &self.diff_cu_qp_delta_depth)?;
        }

        encode!(
            bs,
            &self.pps_cb_qp_offset,
            &self.pps_cr_qp_offset,
            &self.pps_slice_chroma_qp_offsets_present_flag,
            &self.weighted_pred_flag,
            &self.weighted_bipred_flag,
            &self.transquant_bypass_enabled_flag,
            &self.tiles_enabled_flag,
            &self.entropy_coding_sync_enabled_flag
        )?;

        if self.tiles_enabled_flag.0 != 0 {
            encode!(bs, &self.num_tile_columns_minus1, &self.num_tile_rows_minus1, &self.uniform_spacing_flag)?;
            if self.uniform_spacing_flag.0 != 0 {
                encode!(bs, &self.column_width_minus1, &self.row_height_minus1)?;
            }
            encode!(bs, &self.loop_filter_across_tiles_enabled_flag)?;
        }

        self.remaining_bits.encode(bs)?;

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_picture_parameter_set() {
        let data = [0xc0, 0xf2, 0xc6, 0x8d, 0x09, 0xc0, 0xa0, 0x14, 0x7b, 0x24];
        let mut bs = Bitstream::new(data.iter());

        let pps = PictureParameterSet::decode(&mut bs).unwrap();

        assert_eq!(pps.pps_pic_parameter_set_id.0, 0);
        assert_eq!(pps.pps_seq_parameter_set_id.0, 0);
        assert_eq!(pps.dependent_slice_segments_enabled_flag.0, 0);
        assert_eq!(pps.output_flag_present_flag.0, 0);
        assert_eq!(pps.num_extra_slice_header_bits.0, 0);
        assert_eq!(pps.sign_data_hiding_enabled_flag.0, 0);
        assert_eq!(pps.cabac_init_present_flag.0, 1);
        assert_eq!(pps.num_ref_idx_l0_default_active_minus1.0, 0);
        assert_eq!(pps.num_ref_idx_l1_default_active_minus1.0, 0);
        assert_eq!(pps.init_qp_minus26.0, 0);
        assert_eq!(pps.constrained_intra_pred_flag.0, 0);
        assert_eq!(pps.transform_skip_enabled_flag.0, 0);
        assert_eq!(pps.cu_qp_delta_enabled_flag.0, 1);
        assert_eq!(pps.diff_cu_qp_delta_depth.0, 2);
        assert_eq!(pps.pps_cb_qp_offset.0, -6);
        assert_eq!(pps.pps_cr_qp_offset.0, -6);
        assert_eq!(pps.pps_slice_chroma_qp_offsets_present_flag.0, 0);
        assert_eq!(pps.weighted_pred_flag.0, 0);
        assert_eq!(pps.weighted_bipred_flag.0, 0);
        assert_eq!(pps.transquant_bypass_enabled_flag.0, 0);
        assert_eq!(pps.tiles_enabled_flag.0, 1);
        assert_eq!(pps.entropy_coding_sync_enabled_flag.0, 0);
        assert_eq!(pps.num_tile_columns_minus1.0, 2);
        assert_eq!(pps.num_tile_rows_minus1.0, 0);
        assert_eq!(pps.uniform_spacing_flag.0, 0);

        assert_eq!(bs.next_bits(1), None);

        let mut round_trip = Vec::new();
        pps.encode(&mut BitstreamWriter::new(&mut round_trip)).unwrap();
        assert_eq!(round_trip, data);
    }
}
