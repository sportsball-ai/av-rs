use super::error::{Error, Result};
use crate::{read_all, read_one, AtomData, FourCC, ReadData};
use byteorder::{BigEndian, ReadBytesExt};
use std::io::{Read, Seek};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FragmentHeader {
    pub version: u8,
    pub flags: u32,
    pub sequence_number: u32,
}

impl AtomData for FragmentHeader {
    const TYPE: FourCC = FourCC::MFHD;
}

impl ReadData for FragmentHeader {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            version: reader.read_u8()?,
            flags: reader.read_u24::<BigEndian>()?,
            sequence_number: reader.read_u32::<BigEndian>()?,
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TrackFragmentHeader {
    pub version: u8,
    pub flags: u32,
    pub track_id: u32,
    pub base_data_offset: Option<u64>,
    pub sample_description_index: Option<u32>,
    pub default_sample_duration: Option<u32>,
    pub default_sample_size: Option<u32>,
    pub default_sample_flags: Option<u32>,
    pub duration_is_empty: bool,
}

impl AtomData for TrackFragmentHeader {
    const TYPE: FourCC = FourCC::TFHD;
}

impl ReadData for TrackFragmentHeader {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        let version = reader.read_u8()?;
        let flags = reader.read_u24::<BigEndian>()?;
        Ok(Self {
            version,
            flags,
            track_id: reader.read_u32::<BigEndian>()?,
            base_data_offset: if (flags & 0x1) == 1 { Some(reader.read_u64::<BigEndian>()?) } else { None },
            sample_description_index: if (flags & 0x2) != 0 { Some(reader.read_u32::<BigEndian>()?) } else { None },
            default_sample_duration: if (flags & 0x8) != 0 { Some(reader.read_u32::<BigEndian>()?) } else { None },
            default_sample_size: if (flags & 0x10) != 0 { Some(reader.read_u32::<BigEndian>()?) } else { None },
            default_sample_flags: if (flags & 0x20) != 0 { Some(reader.read_u32::<BigEndian>()?) } else { None },
            duration_is_empty: flags & 0x10000 != 0,
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TrackFragmentDecodeTime {
    pub version: u8,
    pub flags: u32,
    pub base_media_decode_time: u32,
}

impl AtomData for TrackFragmentDecodeTime {
    const TYPE: FourCC = FourCC::TFDT;
}

impl ReadData for TrackFragmentDecodeTime {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        let version = reader.read_u8()?;
        Ok(Self {
            version,
            flags: reader.read_u24::<BigEndian>()?,
            base_media_decode_time: if version == 1 {
                reader.read_u8()? as u32
            } else {
                reader.read_u32::<BigEndian>()?
            },
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TrackFragmentRunSampleData {
    pub sample_duration: Option<u32>,
    pub sample_size: Option<u32>,
    pub sample_flags: Option<u32>,
    pub sample_composition_time_offset: Option<u32>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TrackFragmentRun {
    pub version: u8,
    pub flags: u32,
    pub sample_count: u32,
    pub data_offset: Option<i32>,
    pub first_sample_flags: Option<u32>,
    pub sample_data: Vec<TrackFragmentRunSampleData>,
}

impl AtomData for TrackFragmentRun {
    const TYPE: FourCC = FourCC::TRUN;
}

impl ReadData for TrackFragmentRun {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        let version = reader.read_u8()?;
        let flags = reader.read_u24::<BigEndian>()?;
        let sample_count = reader.read_u32::<BigEndian>()?;
        let data_offset = if flags & 0x1 != 0 { Some(reader.read_i32::<BigEndian>()?) } else { None };
        let first_sample_flags = if flags & 0x4 != 0 { Some(reader.read_u32::<BigEndian>()?) } else { None };
        let sample_duration_present = flags & 0x100 != 0;
        let sample_size_present = flags & 0x200 != 0;
        let sample_flags_present = flags & 0x400 != 0;
        let sample_composition_time_offsets_present = flags & 0x800 != 0;
        let mut sample_data = vec![];
        for _ in 0..sample_count {
            let sample_duration = if sample_duration_present {
                Some(reader.read_u32::<BigEndian>()?)
            } else {
                None
            };
            let sample_size = if sample_size_present { Some(reader.read_u32::<BigEndian>()?) } else { None };
            let sample_flags = if sample_flags_present { Some(reader.read_u32::<BigEndian>()?) } else { None };
            let sample_composition_time_offset = if sample_composition_time_offsets_present {
                Some(reader.read_u32::<BigEndian>()?)
            } else {
                None
            };
            sample_data.push(TrackFragmentRunSampleData {
                sample_duration,
                sample_size,
                sample_flags,
                sample_composition_time_offset,
            });
        }

        Ok(Self {
            version,
            flags,
            sample_count,
            data_offset,
            first_sample_flags,
            sample_data,
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TrackFragment {
    pub track_fragment_header: TrackFragmentHeader, // tfhd
    pub track_fragment_runs: Vec<TrackFragmentRun>, // trun
}

impl AtomData for TrackFragment {
    const TYPE: FourCC = FourCC::TRAF;
}

impl ReadData for TrackFragment {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            track_fragment_header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing movie track fragment header"))?,
            track_fragment_runs: read_all(&mut reader)?,
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MovieFragment {
    pub fragment_header: FragmentHeader,     // mfhd
    pub track_fragments: Vec<TrackFragment>, // traf
}

impl AtomData for MovieFragment {
    const TYPE: FourCC = FourCC::MOOF;
}

impl ReadData for MovieFragment {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            fragment_header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing movie header"))?,
            track_fragments: read_all(&mut reader)?,
        })
    }
}
