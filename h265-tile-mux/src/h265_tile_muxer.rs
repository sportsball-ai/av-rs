use bytes::Bytes;
use h265::{
    Bitstream, BitstreamWriter, Decode, EmulationPrevention, Encode, NALUnit, NALUnitHeader, PictureParameterSet, ReadAnnexB, SequenceParameterSet,
    SliceSegmentHeader,
};
use std::io::{self, Read, Result, Write};

struct H265TileMuxerInput<T> {
    nalus: T,
    pps: Option<PictureParameterSet>,
    sps: Option<SequenceParameterSet>,
}

impl<T: Iterator<Item = Result<Bytes>>> H265TileMuxerInput<T> {
    fn next_nalu(&mut self) -> Option<Result<Bytes>> {
        Some(match self.nalus.next()? {
            Ok(b) => self.inspect_nalu(&b).map(|_| b),
            Err(e) => Err(e),
        })
    }

    fn next_slice_segment(&mut self) -> Option<Result<Bytes>> {
        loop {
            let nalu = match self.nalus.next()? {
                Ok(b) => b,
                Err(e) => return Some(Err(e)),
            };
            let nalu_header = match self.inspect_nalu(&nalu) {
                Ok(h) => h,
                Err(e) => return Some(Err(e)),
            };
            match nalu_header.nal_unit_type.0 {
                1..=9 | 16..=21 => return Some(Ok(nalu)),
                _ => {}
            }
        }
    }

    fn inspect_nalu(&mut self, b: &Bytes) -> Result<NALUnitHeader> {
        let mut bs = Bitstream::new(b);
        let nalu_header = NALUnitHeader::decode(&mut bs)?;
        match nalu_header.nal_unit_type.0 {
            h265::NAL_UNIT_TYPE_PPS_NUT => {
                let bs = Bitstream::new(b);
                let mut nalu = NALUnit::decode(bs)?;
                let mut rbsp = Bitstream::new(&mut nalu.rbsp_byte);
                let pps = PictureParameterSet::decode(&mut rbsp)?;
                self.pps = Some(pps);
            }
            h265::NAL_UNIT_TYPE_SPS_NUT => {
                let bs = Bitstream::new(b);
                let mut nalu = NALUnit::decode(bs)?;
                let mut rbsp = Bitstream::new(&mut nalu.rbsp_byte);
                let sps = SequenceParameterSet::decode(&mut rbsp)?;
                self.sps = Some(sps);
            }
            _ => {}
        }
        Ok(nalu_header)
    }
}

pub struct H265TileMuxer<R> {
    inputs: Vec<H265TileMuxerInput<ReadAnnexB<R>>>,
    selection: Vec<usize>,
}

impl<R: Read> H265TileMuxer<R> {
    pub fn new(inputs: Vec<R>, selection: Vec<usize>) -> Self {
        Self {
            inputs: inputs
                .into_iter()
                .map(|r| H265TileMuxerInput {
                    nalus: h265::read_annex_b(r),
                    pps: None,
                    sps: None,
                })
                .collect(),
            selection,
        }
    }

    pub fn mux<W: Write>(mut self, mut output: W) -> Result<()> {
        while let Some(nalu) = self.inputs[0].next_nalu().transpose()? {
            let mut bs = Bitstream::new(&nalu);
            let nalu_header = NALUnitHeader::decode(&mut bs)?;
            match nalu_header.nal_unit_type.0 {
                1..=9 | 16..=21 => {
                    // we can't do anything until we get the pps and sps
                    if self.inputs[0].pps.is_none() || self.inputs[0].sps.is_none() {
                        continue;
                    }

                    struct SliceSegment {
                        header: SliceSegmentHeader,
                        tile_data: Vec<u8>,
                        tiles: Vec<(usize, usize)>,
                    }

                    // gather all the slice segments
                    let mut segments = self
                        .inputs
                        .iter_mut()
                        .enumerate()
                        .map(|(i, input)| {
                            // get the slice segment nalu
                            let nalu = match i {
                                0 => nalu.clone(),
                                _ => match input.next_slice_segment() {
                                    Some(r) => r,
                                    None => Err(io::Error::new(
                                        io::ErrorKind::Other,
                                        "input streams do not have the same number of slice segment nalus",
                                    )),
                                }?,
                            };

                            // make sure we have the sps and pps
                            let pps = match &input.pps {
                                Some(pps) => pps,
                                None => return Err(io::Error::new(io::ErrorKind::Other, "missing input stream pps")),
                            };
                            let sps = match &input.sps {
                                Some(sps) => sps,
                                None => return Err(io::Error::new(io::ErrorKind::Other, "missing input stream sps")),
                            };

                            // parse the header
                            let bs = Bitstream::new(&nalu);
                            let mut nalu = NALUnit::decode(bs)?;
                            let header = SliceSegmentHeader::decode(&mut Bitstream::new(&mut nalu.rbsp_byte), nalu_header.nal_unit_type.0, sps, pps)?;
                            let mut tile_data = nalu.rbsp_byte.into_inner().copied().collect::<Vec<u8>>();

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

                            Ok(SliceSegment { header, tiles, tile_data })
                        })
                        .collect::<Result<Vec<SliceSegment>>>()?;

                    // update the entry points in the header
                    for tile in 0..segments[0].header.num_entry_point_offsets.0 as usize {
                        segments[0].header.offset_len_minus1.0 = segments[0]
                            .header
                            .offset_len_minus1
                            .0
                            .max(segments[self.selection[tile]].header.offset_len_minus1.0);
                        segments[0].header.entry_point_offset_minus1[tile] = segments[self.selection[tile]].header.entry_point_offset_minus1[tile];
                    }

                    // write out the new nalu
                    output.write_all(&[0, 0, 0, 1])?;
                    nalu_header.encode(&mut BitstreamWriter::new(&mut output))?;
                    let mut buf = Vec::new();
                    segments[0].header.encode(
                        &mut BitstreamWriter::new(&mut buf),
                        nalu_header.nal_unit_type.0,
                        self.inputs[0].sps.as_ref().unwrap(),
                        self.inputs[0].pps.as_ref().unwrap(),
                    )?;
                    let buf = EmulationPrevention::new(buf).collect::<Vec<u8>>();
                    output.write_all(&buf)?;
                    for tile in 0..segments[0].header.num_entry_point_offsets.0 as usize + 1 {
                        let selection = &segments[self.selection[tile]];
                        let (offset, len) = selection.tiles[tile];
                        output.write_all(&selection.tile_data[offset..offset + len])?;
                    }
                }
                _ => {
                    output.write_all(&[0, 0, 0, 1])?;
                    output.write_all(&nalu)?;
                }
            }
        }

        output.flush()
    }
}
