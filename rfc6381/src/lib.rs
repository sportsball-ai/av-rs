pub fn codec_from_h264_nalu<T: Iterator<Item = u8>>(mut nalu: h264::NALUnit<T>) -> Option<String> {
    if nalu.nal_unit_type.0 == h264::NAL_UNIT_TYPE_SEQUENCE_PARAMETER_SET {
        let mut rbsp = h264::Bitstream::new(&mut nalu.rbsp_byte);
        let leading_bytes = rbsp.next_bits(24)?;
        return Some(format!(
            "avc1.{:02x}{:02x}{:02x}",
            (leading_bytes >> 16) as u8,
            (leading_bytes >> 8) as u8,
            leading_bytes as u8,
        ));
    }
    None
}

pub fn codec_from_h265_nalu<T: Iterator<Item = u8>>(mut nalu: h265::NALUnit<h265::RBSP<T>>) -> Option<String> {
    use h265::Decode;
    if nalu.nal_unit_header.nal_unit_type.0 == h265::NAL_UNIT_TYPE_SPS_NUT {
        let mut rbsp = h265::Bitstream::new(&mut nalu.rbsp_byte);
        let sps = h265::SequenceParameterSet::decode(&mut rbsp).ok()?;
        let ptl = &sps.profile_tier_level;
        return Some(format!(
            "hvc1.{}{}.{:X}.{}{}.{}",
            if ptl.general_profile_space.0 > 0 {
                ((b'A' + (ptl.general_profile_space.0 - 1)) as char).to_string()
            } else {
                "".to_string()
            },
            ptl.general_profile_idc.0,
            ptl.general_profile_compatibility_flags.0.reverse_bits(),
            match ptl.general_tier_flag.0 {
                0 => 'L',
                _ => 'H',
            },
            ptl.general_level_idc.0,
            {
                let mut constraint_bytes = vec![
                    (ptl.general_constraint_flags.0 >> 40) as u8,
                    (ptl.general_constraint_flags.0 >> 32) as u8,
                    (ptl.general_constraint_flags.0 >> 24) as u8,
                    (ptl.general_constraint_flags.0 >> 16) as u8,
                    (ptl.general_constraint_flags.0 >> 8) as u8,
                    ptl.general_constraint_flags.0 as u8,
                ];
                while constraint_bytes.len() > 1 && constraint_bytes.last().copied() == Some(0) {
                    constraint_bytes.pop();
                }
                constraint_bytes.into_iter().map(|b| format!("{:02X}", b)).collect::<Vec<_>>().join(".")
            },
        ));
    }
    None
}

#[cfg(feature = "ffmpeg")]
pub fn codec_from_ffmpeg_codec_context(codec: &ffmpeg::sys::AVCodecContext) -> Option<String> {
    use ffmpeg::sys::AVCodecID;
    match codec.codec_id {
        AVCodecID::AV_CODEC_ID_AAC => Some(format!("mp4a.40.{}", codec.profile + 1)),
        AVCodecID::AV_CODEC_ID_H264 => {
            if codec.extradata_size > 0 {
                let extradata = unsafe { std::slice::from_raw_parts(codec.extradata, codec.extradata_size as _) };
                for nalu in h264::iterate_annex_b(&extradata) {
                    let bs = h264::Bitstream::new(nalu.iter().copied());
                    let nalu = h264::NALUnit::decode(bs).ok()?;
                    if let Some(codec) = codec_from_h264_nalu(nalu) {
                        return Some(codec);
                    }
                }
            }
            None
        }
        AVCodecID::AV_CODEC_ID_HEVC => {
            if codec.extradata_size > 0 {
                let extradata = unsafe { std::slice::from_raw_parts(codec.extradata, codec.extradata_size as _) };
                for nalu in h265::iterate_annex_b(&extradata) {
                    let bs = h264::Bitstream::new(nalu.iter().copied());
                    let nalu = h265::NALUnit::decode(bs).ok()?;
                    if let Some(codec) = codec_from_h265_nalu(nalu) {
                        return Some(codec);
                    }
                }
            }
            None
        }
        _ => None,
    }
}
