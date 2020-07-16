use super::{decode, syntax_elements::*, Bitstream};
use std::io;

#[derive(Debug, Default)]
pub struct ProfileTierLevel {
    // if( profilePresentFlag ) {
    pub general_profile_space: U2,
    pub general_tier_flag: U1,
    pub general_profile_idc: U5,
    // }
    pub general_profile_compatibility_flags: U32,
    pub general_constraint_flags: U48,
    pub general_level_idc: U8,
    // TODO: expose sub-layer info?
}

impl ProfileTierLevel {
    pub fn decode<T: AsRef<[u8]>>(bs: &mut Bitstream<T>, profile_present_flag: u8, max_num_sub_layers_minus1: u8) -> io::Result<Self> {
        let mut ret = Self::default();

        if profile_present_flag != 0 {
            decode!(bs, &mut ret.general_profile_space, &mut ret.general_tier_flag, &mut ret.general_profile_idc)?;
        }

        decode!(
            bs,
            &mut ret.general_profile_compatibility_flags,
            &mut ret.general_constraint_flags,
            &mut ret.general_level_idc
        )?;

        let mut sub_layer_profile_present_flag = vec![];
        let mut sub_layer_level_present_flag = vec![];
        for _ in 0..max_num_sub_layers_minus1 {
            sub_layer_profile_present_flag.push(bs.read_bits(1)?);
            sub_layer_level_present_flag.push(bs.read_bits(1)?);
        }

        if max_num_sub_layers_minus1 > 0 {
            for _ in max_num_sub_layers_minus1..8 {
                bs.read_bits(2)?;
            }
        }

        for i in 0..max_num_sub_layers_minus1 {
            if sub_layer_profile_present_flag[i as usize] != 0 {
                bs.read_bits(88)?;
            }
            if sub_layer_profile_present_flag[i as usize] != 0 {
                bs.read_bits(8)?;
            }
        }

        Ok(ret)
    }
}
