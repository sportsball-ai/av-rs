use std::fs::File;
use std::io;

use h264::{self, read_annex_b, syntax_elements::*, Bitstream, Decode, NALUnit, sei::SEI};

fn main() -> io::Result<()> {
    let mut video = File::open("../../video/000.h264")?;

    for bytes in read_annex_b(&mut video) {
        let bytes = bytes?;
        let bs = Bitstream::new(&bytes);
        let nalu = NALUnit::decode(bs)?;
        if nalu.nal_ref_idc.0 == 0 && nalu.nal_unit_type.0 == h264::NAL_UNIT_TYPE_SUPPLEMENTAL_ENHANCEMENT_INFORMATION {
            let mut bs = Bitstream::new(nalu.rbsp_byte.into_inner());
            let sei = SEI::decode(&mut bs)?;
            println!("sei pic_timing: {:?}", sei.pic_timing.unwrap());
        }
    }
    Ok(())
}
