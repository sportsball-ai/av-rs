use h265::{
    Bitstream, BitstreamWriter, Decode, EmulationPrevention, Encode, NALUnit, NALUnitHeader, PictureParameterSet, SequenceParameterSet, SliceSegmentHeader,
};
use std::io::{self, Write};

/// Given an iterator for slice NALUs that are known to correspond to the same frame, writes out the muxed slice NALU.
pub fn mux_slices<I, T, O>(nalus: I, selection: &[usize], mut output: O, sps: &SequenceParameterSet, pps: &PictureParameterSet) -> io::Result<()>
where
    I: IntoIterator<Item = T>,
    T: AsRef<[u8]>,
    O: Write,
{
    struct SliceSegment {
        nalu_header: NALUnitHeader,
        header: SliceSegmentHeader,
        tile_data: Vec<u8>,
        tiles: Vec<(usize, usize)>,
    }

    // gather all the slice segments
    let mut segments = nalus
        .into_iter()
        .map(|nalu| -> io::Result<_> {
            // parse the header
            let bs = Bitstream::new(nalu.as_ref().iter().copied());
            let mut nalu = NALUnit::decode(bs)?;
            let header = SliceSegmentHeader::decode(&mut Bitstream::new(&mut nalu.rbsp_byte), nalu.nal_unit_header.nal_unit_type.0, sps, pps)?;
            let mut tile_data = nalu.rbsp_byte.into_inner().collect::<Vec<u8>>();

            // drop any cabac_zero_words
            while tile_data.ends_with(&[0, 0, 3]) {
                tile_data.truncate(tile_data.len() - 3);
            }

            // collect the tile offsets
            let mut tiles: Vec<(usize, usize)> = Vec::new();
            let mut offset: usize = 0;
            for entry_offset in &header.entry_point_offset_minus1 {
                let end = offset + *entry_offset as usize + 1;
                tiles.push((offset, end - offset));
                offset = end;
            }
            tiles.push((offset, tile_data.len() - offset));

            Ok(SliceSegment {
                header,
                nalu_header: nalu.nal_unit_header,
                tiles,
                tile_data,
            })
        })
        .collect::<Result<Vec<SliceSegment>, _>>()?;

    // update the entry points in the header
    for tile in 0..segments[0].header.num_entry_point_offsets.0 as usize {
        segments[0].header.offset_len_minus1.0 = segments[0].header.offset_len_minus1.0.max(segments[selection[tile]].header.offset_len_minus1.0);
        segments[0].header.entry_point_offset_minus1[tile] = segments[selection[tile]].header.entry_point_offset_minus1[tile];
    }

    // write out the new nalu
    output.write_all(&[0, 0, 0, 1])?;
    let nalu_header = &segments[0].nalu_header;
    nalu_header.encode(&mut BitstreamWriter::new(&mut output))?;
    let mut buf = Vec::new();
    segments[0]
        .header
        .encode(&mut BitstreamWriter::new(&mut buf), nalu_header.nal_unit_type.0, sps, pps)?;
    let buf = EmulationPrevention::new(buf).collect::<Vec<u8>>();
    output.write_all(&buf)?;
    for tile in 0..segments[0].header.num_entry_point_offsets.0 as usize + 1 {
        let selection = &segments[selection[tile]];
        let (offset, len) = selection.tiles[tile];
        output.write_all(&selection.tile_data[offset..offset + len])?;
    }

    Ok(())
}

/// Given iterators over results of AsRef<[u8]>, this function writes muxed tiles to the given
/// output.
pub fn mux<I, II, T, E, Iter, O>(inputs: II, selection: &[usize], mut output: O) -> Result<(), E>
where
    E: From<io::Error>,
    T: AsRef<[u8]>,
    I: IntoIterator<Item = Result<T, E>, IntoIter = Iter>,
    II: IntoIterator<Item = I>,
    Iter: Iterator<Item = Result<T, E>>,
    O: Write,
{
    let mut inputs: Vec<_> = inputs
        .into_iter()
        .map(|input| Input {
            nalus: input.into_iter(),
            pps: None,
            sps: None,
        })
        .collect();

    while let Some(nalu) = inputs[0].next_nalu().transpose()? {
        let mut bs = Bitstream::new(nalu.as_ref().iter().copied());
        let nalu_header = NALUnitHeader::decode(&mut bs)?;
        match nalu_header.nal_unit_type.0 {
            1..=9 | 16..=21 => {
                // we can't do anything until we get the pps and sps
                if inputs[0].pps.is_none() || inputs[0].sps.is_none() {
                    continue;
                }

                let secondary_nalus = inputs[1..]
                    .iter_mut()
                    .map(|input| match input.next_slice_segment() {
                        Some(r) => r,
                        None => Err(io::Error::new(io::ErrorKind::Other, "input streams do not have the same number of slice segment nalus").into()),
                    })
                    .collect::<Result<Vec<_>, _>>()?;

                let mut nalus = vec![nalu.as_ref()];
                nalus.extend(secondary_nalus.iter().map(|nalu| nalu.as_ref()));

                mux_slices(nalus, selection, &mut output, inputs[0].sps.as_ref().unwrap(), inputs[0].pps.as_ref().unwrap())?;
            }
            _ => {
                output.write_all(&[0, 0, 0, 1])?;
                output.write_all(nalu.as_ref())?;
            }
        }
    }

    output.flush()?;

    Ok(())
}

struct Input<Iter> {
    nalus: Iter,
    pps: Option<PictureParameterSet>,
    sps: Option<SequenceParameterSet>,
}

impl<T: AsRef<[u8]>, Iter: Iterator<Item = Result<T, E>>, E: From<io::Error>> Input<Iter> {
    fn next_nalu(&mut self) -> Option<Result<T, E>> {
        Some(match self.nalus.next()? {
            Ok(b) => self.inspect_nalu(b.as_ref()).map(|_| b).map_err(|e| e.into()),
            Err(e) => Err(e),
        })
    }

    fn next_slice_segment(&mut self) -> Option<Result<T, E>> {
        loop {
            let nalu = match self.nalus.next()? {
                Ok(b) => b,
                Err(e) => return Some(Err(e)),
            };
            let header = match self.inspect_nalu(nalu.as_ref()) {
                Ok(h) => h,
                Err(e) => return Some(Err(e.into())),
            };
            match header.nal_unit_type.0 {
                1..=9 | 16..=21 => return Some(Ok(nalu)),
                _ => {}
            }
        }
    }

    fn inspect_nalu(&mut self, nalu: &[u8]) -> io::Result<NALUnitHeader> {
        let bs = Bitstream::new(nalu.iter().copied());
        let mut nalu = NALUnit::decode(bs)?;
        match nalu.nal_unit_header.nal_unit_type.0 {
            h265::NAL_UNIT_TYPE_PPS_NUT => {
                let mut rbsp = Bitstream::new(&mut nalu.rbsp_byte);
                let pps = PictureParameterSet::decode(&mut rbsp)?;
                self.pps = Some(pps);
            }
            h265::NAL_UNIT_TYPE_SPS_NUT => {
                let mut rbsp = Bitstream::new(&mut nalu.rbsp_byte);
                let sps = SequenceParameterSet::decode(&mut rbsp)?;
                self.sps = Some(sps);
            }
            _ => {}
        }
        Ok(nalu.nal_unit_header)
    }
}
