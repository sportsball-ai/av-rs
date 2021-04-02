use h265::{
    Bitstream, BitstreamWriter, Decode, EmulationPrevention, Encode, NALUnit, NALUnitHeader, PictureParameterSet, SequenceParameterSet, SliceSegmentHeader,
};
use std::{
    collections::VecDeque,
    io::{self, Write},
};

pub fn join<I, II, T, E, Iter, O>(inputs: II, mut output: O) -> Result<(), E>
where
    E: From<io::Error>,
    T: AsRef<[u8]>,
    I: IntoIterator<Item = Result<T, E>, IntoIter = Iter>,
    II: IntoIterator<Item = I>,
    Iter: Iterator<Item = Result<T, E>>,
    O: Write,
{
    let mut inputs = inputs.into_iter().map(|input| Input::new(input.into_iter())).collect::<Result<Vec<_>, E>>()?;

    let input_0_sps = inputs[0].sps.clone();
    let mut sps = input_0_sps.clone();
    sps.pic_width_in_luma_samples.0 = 0;
    for input in &inputs {
        sps.pic_width_in_luma_samples.0 += input.sps.pic_width_in_luma_samples.0;
    }

    let input_0_pps = inputs[0].pps.clone();
    let mut pps = input_0_pps.clone();
    pps.tiles_enabled_flag.0 = 1;
    pps.num_tile_columns_minus1.0 = inputs.len() as u64 - 1;
    pps.uniform_spacing_flag.0 = 1;

    while let Some(nalu) = inputs[0].next_nalu() {
        let nalu = nalu?;

        let mut bs = Bitstream::new(nalu.iter().copied());
        let nalu_header = NALUnitHeader::decode(&mut bs)?;

        match nalu_header.nal_unit_type.0 {
            h265::NAL_UNIT_TYPE_SPS_NUT => {
                output.write_all(&[0, 0, 0, 1])?;
                nalu_header.encode(&mut BitstreamWriter::new(&mut output))?;
                let mut buf = Vec::new();
                sps.encode(&mut BitstreamWriter::new(&mut buf))?;
                let buf = EmulationPrevention::new(buf).collect::<Vec<u8>>();
                output.write_all(&buf)?;
            }
            h265::NAL_UNIT_TYPE_PPS_NUT => {
                output.write_all(&[0, 0, 0, 1])?;
                nalu_header.encode(&mut BitstreamWriter::new(&mut output))?;
                let mut buf = Vec::new();
                pps.encode(&mut BitstreamWriter::new(&mut buf))?;
                let buf = EmulationPrevention::new(buf).collect::<Vec<u8>>();
                output.write_all(&buf)?;
            }
            1..=9 | 16..=21 => {
                // parse the header
                let bs = Bitstream::new(nalu.iter().copied());
                let mut nalu = NALUnit::decode(bs)?;
                let mut header = SliceSegmentHeader::decode(
                    &mut Bitstream::new(&mut nalu.rbsp_byte),
                    nalu.nal_unit_header.nal_unit_type.0,
                    &input_0_sps,
                    &input_0_pps,
                )?;

                // write out the new nalus

                let slice_data = nalu.rbsp_byte.into_inner().collect::<Vec<u8>>();
                output.write_all(&[0, 0, 0, 1])?;
                nalu_header.encode(&mut BitstreamWriter::new(&mut output))?;
                let mut buf = Vec::new();
                header.encode(&mut BitstreamWriter::new(&mut buf), nalu_header.nal_unit_type.0, &sps, &pps)?;
                let buf = EmulationPrevention::new(buf).collect::<Vec<u8>>();
                output.write_all(&buf)?;
                output.write_all(&slice_data)?;

                let mut ctb_x = input_0_sps.PicWidthInCtbsY();
                for input in &mut inputs[1..] {
                    header.first_slice_segment_in_pic_flag.0 = 0;
                    header.slice_segment_address = ctb_x;
                    ctb_x += input.sps.PicWidthInCtbsY();

                    loop {
                        let nalu = match input.next_nalu() {
                            Some(nalu) => nalu?,
                            None => return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "matching slice segment not found").into()),
                        };
                        let bs = Bitstream::new(nalu.iter().copied());
                        let mut nalu = NALUnit::decode(bs)?;

                        match nalu.nal_unit_header.nal_unit_type.0 {
                            1..=9 | 16..=21 => {
                                SliceSegmentHeader::decode(
                                    &mut Bitstream::new(&mut nalu.rbsp_byte),
                                    nalu.nal_unit_header.nal_unit_type.0,
                                    &input_0_sps,
                                    &input_0_pps,
                                )?;
                                output.write_all(&[0, 0, 0, 1])?;
                                nalu_header.encode(&mut BitstreamWriter::new(&mut output))?;
                                let mut buf = Vec::new();
                                header.encode(&mut BitstreamWriter::new(&mut buf), nalu_header.nal_unit_type.0, &sps, &pps)?;
                                let buf = EmulationPrevention::new(buf).collect::<Vec<u8>>();
                                output.write_all(&buf)?;
                                let slice_data = nalu.rbsp_byte.into_inner().collect::<Vec<u8>>();
                                output.write_all(&slice_data)?;
                                break;
                            }
                            _ => {}
                        }
                    }
                }
            }
            _ => {
                output.write_all(&[0, 0, 0, 1])?;
                output.write_all(nalu)?
            }
        }
    }

    Ok(())
}

struct Input<Iter, T> {
    skipped_nalus: VecDeque<T>,
    nalus: Iter,
    pps: PictureParameterSet,
    sps: SequenceParameterSet,
    current_nalu: Option<T>,
}

impl<T: AsRef<[u8]>, Iter: Iterator<Item = Result<T, E>>, E: From<io::Error>> Input<Iter, T> {
    fn new(mut nalus: Iter) -> Result<Self, E> {
        let mut skipped_nalus = VecDeque::new();
        let mut sps = None;
        let mut pps = None;
        while let Some(nalu) = nalus.next() {
            let nalu = nalu?;
            {
                let bs = Bitstream::new(nalu.as_ref().iter().copied());
                let mut nalu = NALUnit::decode(bs)?;
                match nalu.nal_unit_header.nal_unit_type.0 {
                    h265::NAL_UNIT_TYPE_PPS_NUT => {
                        let mut rbsp = Bitstream::new(&mut nalu.rbsp_byte);
                        pps = Some(PictureParameterSet::decode(&mut rbsp)?);
                    }
                    h265::NAL_UNIT_TYPE_SPS_NUT => {
                        let mut rbsp = Bitstream::new(&mut nalu.rbsp_byte);
                        sps = Some(SequenceParameterSet::decode(&mut rbsp)?);
                    }
                    _ => {}
                }
            }
            skipped_nalus.push_back(nalu);
            if sps.is_some() && pps.is_some() {
                break;
            }
        }
        match (sps, pps) {
            (Some(sps), Some(pps)) => Ok(Self {
                skipped_nalus,
                nalus,
                sps,
                pps,
                current_nalu: None,
            }),
            _ => Err(io::Error::new(io::ErrorKind::UnexpectedEof, "parameter sets not found").into()),
        }
    }

    fn next_nalu(&mut self) -> Option<Result<&[u8], E>> {
        self.current_nalu = Some(if let Some(next) = self.skipped_nalus.pop_front() {
            next
        } else {
            match self.nalus.next()? {
                Ok(nalu) => nalu,
                Err(e) => return Some(Err(e)),
            }
        });
        Some(Ok(self.current_nalu.as_ref().expect("we just set this").as_ref()))
    }
}
