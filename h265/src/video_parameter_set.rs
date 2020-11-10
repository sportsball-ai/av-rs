use super::{decode, syntax_elements::*, Bitstream, Decode, ProfileTierLevel};
use std::io;

#[derive(Debug, Default)]
pub struct VideoParameterSetSubLayerOrderingInfo {
    pub vps_max_dec_pic_buffering_minus1: UE,
    pub vps_max_num_reorder_pics: UE,
    pub vps_max_latency_increase_plus1: UE,
}

impl Decode for VideoParameterSetSubLayerOrderingInfo {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        Ok(Self {
            vps_max_dec_pic_buffering_minus1: UE::decode(bs)?,
            vps_max_num_reorder_pics: UE::decode(bs)?,
            vps_max_latency_increase_plus1: UE::decode(bs)?,
        })
    }
}

// ITU-T H.265, 11/2019 F.7.3.2.1
#[derive(Debug, Default)]
pub struct VideoParameterSet {
    pub vps_video_parameter_set_id: U4,
    pub vps_base_layer_internal_flag: U1,
    pub vps_base_layer_available_flag: U1,
    pub vps_max_layers_minus1: U6,
    pub vps_max_sub_layers_minus1: U3,
    pub vps_temporal_id_nesting_flag: U1,
    pub vps_reserved_0xffff_16bits: U16,
    pub profile_tier_level: ProfileTierLevel,
    pub vps_sub_layer_ordering_info_present_flag: U1,

    // for( i = ( vps_sub_layer_ordering_info_present_flag ? 0 : vps_max_sub_layers_minus1 ); i <= vps_max_sub_layers_minus1; i++ )
    pub sub_layer_ordering_info: Vec<VideoParameterSetSubLayerOrderingInfo>,

    pub vps_max_layer_id: U6,
    pub vps_num_layer_sets_minus1: UE,
    // for( i = 1; i <= vps_num_layer_sets_minus1; i++ )
    // for( j = 0; j <= vps_max_layer_id; j++ )
    pub layer_id_included_flag: Vec<Vec<U1>>,

    pub vps_timing_info_present_flag: U1,

    // if( vps_timing_info_present_flag ) {
    pub vps_num_units_in_tick: U32,
    pub vps_time_scale: U32,
    // TODO: decode remaining fields?
}

impl Decode for VideoParameterSet {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut ret = Self::default();

        decode!(
            bs,
            &mut ret.vps_video_parameter_set_id,
            &mut ret.vps_base_layer_internal_flag,
            &mut ret.vps_base_layer_available_flag,
            &mut ret.vps_max_layers_minus1,
            &mut ret.vps_max_sub_layers_minus1,
            &mut ret.vps_temporal_id_nesting_flag,
            &mut ret.vps_reserved_0xffff_16bits
        )?;

        ret.profile_tier_level = ProfileTierLevel::decode(bs, 1, ret.vps_max_sub_layers_minus1.0)?;

        decode!(bs, &mut ret.vps_sub_layer_ordering_info_present_flag)?;

        let mut i = match ret.vps_sub_layer_ordering_info_present_flag.0 {
            0 => ret.vps_max_sub_layers_minus1.0,
            _ => 0,
        };
        while i <= ret.vps_max_sub_layers_minus1.0 {
            ret.sub_layer_ordering_info.push(VideoParameterSetSubLayerOrderingInfo::decode(bs)?);
            i += 1;
        }

        decode!(bs, &mut ret.vps_max_layer_id, &mut ret.vps_num_layer_sets_minus1)?;

        ret.layer_id_included_flag.push(vec![U1(0); ret.vps_max_layer_id.0 as usize + 1]);
        for _ in 1..=ret.vps_num_layer_sets_minus1.0 {
            let mut v = vec![];
            for _ in 0..=ret.vps_max_layer_id.0 {
                v.push(U1::decode(bs)?);
            }
            ret.layer_id_included_flag.push(v);
        }

        decode!(bs, &mut ret.vps_timing_info_present_flag)?;

        if ret.vps_timing_info_present_flag.0 != 0 {
            decode!(bs, &mut ret.vps_num_units_in_tick, &mut ret.vps_time_scale)?;
        }

        Ok(ret)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_sequence_parameter_set() {
        let mut bs = Bitstream::new(vec![
            0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x17, 0x02, 0x40,
        ]);

        let vps = VideoParameterSet::decode(&mut bs).unwrap();

        assert_eq!(0x6000_0000, vps.profile_tier_level.general_profile_compatibility_flags.0);
        assert_eq!(0xb000_0000_0000, vps.profile_tier_level.general_constraint_flags.0);
        assert_eq!(0, vps.vps_timing_info_present_flag.0);

        let mut bs = Bitstream::new(vec![
            0x0c, 0x01, 0xff, 0xff, 0x04, 0x08, 0x00, 0x00, 0x00, 0x9d, 0x08, 0x00, 0x00, 0x00, 0x00, 0x78, 0x95, 0x98, 0x09,
        ]);

        let vps = VideoParameterSet::decode(&mut bs).unwrap();

        assert_eq!(0x800_0000, vps.profile_tier_level.general_profile_compatibility_flags.0);
        assert_eq!(0x9d08_0000_0000, vps.profile_tier_level.general_constraint_flags.0);
        assert_eq!(0, vps.vps_timing_info_present_flag.0);
    }
}
