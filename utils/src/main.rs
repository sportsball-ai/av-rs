use std::fs::File;
use std::io;

use h264::{self, read_annex_b, sei::SEI, Bitstream, Decode, NALUnit, SequenceParameterSet, VUIParameters};

fn main() -> io::Result<()> {
    let mut video = File::open("../../video/000.h264")?;
    let mut last_vui_parameters: Option<VUIParameters> = None;

    for bytes in read_annex_b(&mut video) {
        let bytes = bytes?;
        let bs = Bitstream::new(bytes);
        let nalu = NALUnit::decode(bs)?;

        match nalu.nal_unit_type.0 {
            h264::NAL_UNIT_TYPE_SEQUENCE_PARAMETER_SET => {
                let mut bs = Bitstream::new(nalu.rbsp_byte.into_inner());
                let sps = SequenceParameterSet::decode(&mut bs)?;
                last_vui_parameters = Some(sps.vui_parameters);
            }
            h264::NAL_UNIT_TYPE_SUPPLEMENTAL_ENHANCEMENT_INFORMATION if nalu.nal_ref_idc.0 == 0 => {
                let mut bs = Bitstream::new(nalu.rbsp_byte.into_inner());
                if let Some(vui_params) = &last_vui_parameters {
                    let sei = SEI::decode(&mut bs, &vui_params)?;
                    println!("sei pic_timing: {:?}", sei.pic_timing.unwrap());
                }
            }
            _ => {}
        }
    }
    Ok(())
}
