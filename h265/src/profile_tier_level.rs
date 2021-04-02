use super::{decode, encode, syntax_elements::*, Bitstream, BitstreamWriter, Decode, Encode};
use std::io;

#[derive(Clone, Debug, Default)]
pub struct ProfileTierLevel {
    // if( profilePresentFlag ) {
    pub general_profile_space: U2,
    pub general_tier_flag: U1,
    pub general_profile_idc: U5,
    pub general_profile_compatibility_flags: U32,
    pub general_constraint_flags: U48,
    // }
    pub general_level_idc: U8,

    pub sub_layer_profile_present_flag: Vec<U1>,
    pub sub_layer_level_present_flag: Vec<U1>,

    // TODO: expose sub-layer info?
    pub sublayer_info_bits: Vec<U1>,
}

impl ProfileTierLevel {
    pub fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>, profile_present_flag: u8, max_num_sub_layers_minus1: u8) -> io::Result<Self> {
        let mut ret = Self::default();

        if profile_present_flag != 0 {
            decode!(
                bs,
                &mut ret.general_profile_space,
                &mut ret.general_tier_flag,
                &mut ret.general_profile_idc,
                &mut ret.general_profile_compatibility_flags,
                &mut ret.general_constraint_flags
            )?;
        }

        decode!(bs, &mut ret.general_level_idc)?;

        for _ in 0..max_num_sub_layers_minus1 {
            ret.sub_layer_profile_present_flag.push(U1::decode(bs)?);
            ret.sub_layer_level_present_flag.push(U1::decode(bs)?);
        }

        if max_num_sub_layers_minus1 > 0 {
            for _ in max_num_sub_layers_minus1..8 {
                ret.sublayer_info_bits.extend(bs.bits().take(2).map(|b| U1(b as _)));
            }
        }

        for i in 0..max_num_sub_layers_minus1 {
            if ret.sub_layer_profile_present_flag[i as usize].0 != 0 {
                ret.sublayer_info_bits.extend(bs.bits().take(88).map(|b| U1(b as _)));
            }
            if ret.sub_layer_level_present_flag[i as usize].0 != 0 {
                ret.sublayer_info_bits.extend(bs.bits().take(8).map(|b| U1(b as _)));
            }
        }

        Ok(ret)
    }

    pub fn encode<T: io::Write>(&self, bs: &mut BitstreamWriter<T>, profile_present_flag: u8, max_num_sub_layers_minus1: u8) -> io::Result<()> {
        if profile_present_flag != 0 {
            encode!(bs, &self.general_profile_space, &self.general_tier_flag, &self.general_profile_idc)?;
        }

        encode!(
            bs,
            &self.general_profile_compatibility_flags,
            &self.general_constraint_flags,
            &self.general_level_idc
        )?;

        for i in 0..max_num_sub_layers_minus1 {
            self.sub_layer_profile_present_flag[i as usize].encode(bs)?;
            self.sub_layer_level_present_flag[i as usize].encode(bs)?;
        }

        self.sublayer_info_bits.encode(bs)?;

        Ok(())
    }
}
