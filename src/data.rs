use super::atom::{Atom, AtomReader, FourCC, AtomWriteExt};
use super::deserializer::Deserializer;
use super::serializer::Serializer;
use super::error::{Error, Result};

use std::convert::TryInto;
use std::fmt;
use std::io::{Cursor, Read, Seek, Write};

use byteorder::{BigEndian, ByteOrder, WriteBytesExt};
use serde::{de, ser};

pub fn read_one<T: AtomData, R: Read + Seek>(mut reader: R) -> Result<Option<T>> {
    match AtomReader::new(&mut reader).find(|a| match a {
        Ok(a) => a.typ == T::TYPE,
        Err(_) => true,
    }) {
        Some(Ok(a)) => Ok(Some(T::read(a.data(&mut reader))?)),
        Some(Err(err)) => Err(err.into()),
        None => Ok(None),
    }
}

pub fn read_all<T: AtomData, R: Read + Seek>(mut reader: R) -> Result<Vec<T>> {
    let atoms = AtomReader::new(&mut reader).filter(|a| match a {
        Ok(a) => a.typ == T::TYPE,
        Err(_) => true,
    }).map(|r| r.map_err(|e| e.into())).collect::<Result<Vec<Atom>>>()?;
    atoms.iter().map(|a| T::read(a.data(&mut reader))).collect()
}

pub trait ReadData: Sized {
    fn read<R: Read + Seek>(reader: R) -> Result<Self>;
}

impl<'de, T: de::Deserialize<'de>> ReadData for T {
    fn read<R: Read + Seek>(reader: R) -> Result<Self> {
        let mut d = Deserializer::new(reader);
        Self::deserialize(&mut d)
    }
}

pub trait WriteData: Sized {
    fn write<W: Write>(&self, writer: W) -> Result<()>;
}

impl<T: ser::Serialize> WriteData for T {
    fn write<W: Write>(&self, writer: W) -> Result<()> {
        let mut s = Serializer::new(writer);
        self.serialize(&mut s)
    }
}

pub trait AtomData: ReadData {
    const TYPE: FourCC;
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FixedPoint16(f32);

impl ser::Serialize for FixedPoint16 {
    fn serialize<S: ser::Serializer>(&self, serializer: S) -> std::result::Result<S::Ok, S::Error> {
        serializer.serialize_u16((self.0 * (0x100 as f32)) as _)
    }
}

impl<'de> de::Deserialize<'de> for FixedPoint16 {
    fn deserialize<D: de::Deserializer<'de>>(deserializer: D) -> std::result::Result<FixedPoint16, D::Error> {
        deserializer.deserialize_u16(FixedPoint16Visitor)
    }
}

struct FixedPoint16Visitor;

impl<'de> de::Visitor<'de> for FixedPoint16Visitor {
    type Value = FixedPoint16;

    fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        formatter.write_str("a fixed point number")
    }

    fn visit_u16<E: de::Error>(self, value: u16) -> std::result::Result<Self::Value, E> {
        Ok(((value as f32) / (0x100 as f32)).into())
    }
}

impl From<f32> for FixedPoint16 {
    fn from(v: f32) -> Self {
        Self(v)
    }
}

impl From<FixedPoint16> for f32 {
    fn from(v: FixedPoint16) -> f32 {
        v.0
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FixedPoint32(f32);

impl ser::Serialize for FixedPoint32 {
    fn serialize<S: ser::Serializer>(&self, serializer: S) -> std::result::Result<S::Ok, S::Error> {
        serializer.serialize_u32((self.0 * (0x10000 as f32)) as _)
    }
}

impl<'de> de::Deserialize<'de> for FixedPoint32 {
    fn deserialize<D: de::Deserializer<'de>>(deserializer: D) -> std::result::Result<FixedPoint32, D::Error> {
        deserializer.deserialize_u32(FixedPoint32Visitor)
    }
}

struct FixedPoint32Visitor;

impl<'de> de::Visitor<'de> for FixedPoint32Visitor {
    type Value = FixedPoint32;

    fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        formatter.write_str("a fixed point number")
    }

    fn visit_u32<E: de::Error>(self, value: u32) -> std::result::Result<Self::Value, E> {
        Ok(((value as f32) / (0x10000 as f32)).into())
    }
}

impl From<f32> for FixedPoint32 {
    fn from(v: f32) -> Self {
        Self(v)
    }
}

impl From<FixedPoint32> for f32 {
    fn from(v: FixedPoint32) -> f32 {
        v.0
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct MovieData {
    pub header: MovieHeaderData,
    pub tracks: Vec<TrackData>,
}

impl AtomData for MovieData {
    const TYPE: FourCC = FourCC(0x6d6f6f76);
}

impl ReadData for MovieData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing movie header"))?,
            tracks: read_all(&mut reader)?,
        })
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct MovieHeaderData {
    pub version: u8,
    pub flags: [u8; 3],
    pub creation_time: u32,
    pub modification_time: u32,
    pub time_scale: u32,
    pub duration: u32,
    pub preferred_rate: FixedPoint32,
    pub preferred_volume: FixedPoint16,
    pub reserved: [u8; 10],
    pub matrix_structure: [u32; 9],
    pub preview_time: u32,
    pub preview_duration: u32,
    pub poster_time: u32,
    pub selection_time: u32,
    pub selection_duration: u32,
    pub current_time: u32,
    pub next_track_id: u32,
}

impl AtomData for MovieHeaderData {
    const TYPE: FourCC = FourCC(0x6d766864);
}

#[derive(Clone, Debug, PartialEq)]
pub struct TrackData {
    pub header: TrackHeaderData,
    pub media: MediaData,
    pub edit: Option<EditData>,
}

impl AtomData for TrackData {
    const TYPE: FourCC = FourCC(0x7472616b);
}

impl ReadData for TrackData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing track header"))?,
            media: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing track media"))?,
            edit: read_one(&mut reader)?,
        })
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct TrackHeaderData {
    pub version: u8,
    pub flags: [u8; 3],
    pub creation_time: u32,
    pub modification_time: u32,
    pub id: u32,
    pub reserved: u32,
    pub duration: u32,
    pub reserved2: [u8; 8],
    pub layer: u16,
    pub alternate_group: u16,
    pub volume: FixedPoint16,
    pub reserved3: u16,
    pub matrix_structure: [u32; 9],
    pub width: FixedPoint32,
    pub height: FixedPoint32,
}

impl AtomData for TrackHeaderData {
    const TYPE: FourCC = FourCC(0x746b6864);
}

#[derive(Clone, Debug, PartialEq)]
pub enum MediaInformationData {
    Sound(SoundMediaInformationData),
    Video(VideoMediaInformationData),
    Base(BaseMediaInformationData),
}

#[derive(Clone, Debug, PartialEq)]
pub struct MediaData {
    pub header: MediaHeaderData,
    pub handler_reference: Option<HandlerReferenceData>,
    pub information: Option<MediaInformationData>,
}

impl AtomData for MediaData {
    const TYPE: FourCC = FourCC(0x6d646961);
}

impl ReadData for MediaData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        let handler_reference: Option<HandlerReferenceData> = read_one(&mut reader)?;
        Ok(Self{
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media header"))?,
            information: {
                let component_subtype = handler_reference.as_ref().and_then(|v| v.component_subtype.to_string()).unwrap_or("".to_string());
                match component_subtype.as_str() {
                    "vide" => Some(MediaInformationData::Video(read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media video information"))?)),
                    "soun" => Some(MediaInformationData::Sound(read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media sound information"))?)),
                    _ => Some(MediaInformationData::Base(read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media base information"))?)),
                }
            },
            handler_reference: handler_reference,
        })
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct MediaHeaderData {
    pub version: u8,
    pub flags: [u8; 3],
    pub creation_time: u32,
    pub modification_time: u32,
    pub time_scale: u32,
    pub duration: u32,
    pub language: u16,
    pub quality: u16,
}

impl AtomData for MediaHeaderData {
    const TYPE: FourCC = FourCC(0x6d646864);
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct HandlerReferenceData {
    pub version: u8,
    pub flags: [u8; 3],
    pub component_type: FourCC,
    pub component_subtype: FourCC,
    pub component_manufacturer: u32,
    pub component_flags: u32,
    pub component_flags_mask: u32,
}

impl AtomData for HandlerReferenceData {
    const TYPE: FourCC = FourCC(0x68646c72);
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct ChunkOffset64Data {
    pub version: u8,
    pub flags: [u8; 3],
    pub offsets: Vec<u64>,
}

impl AtomData for ChunkOffset64Data {
    const TYPE: FourCC = FourCC(0x636f3634);
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq)]
pub struct ChunkOffsetData {
    pub version: u8,
    pub flags: [u8; 3],
    pub offsets: Vec<u32>,
}

impl AtomData for ChunkOffsetData {
    const TYPE: FourCC = FourCC(0x7374636f);
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct SampleSizeData {
    pub version: u8,
    pub flags: [u8; 3],
    pub constant_sample_size: u32,
    pub sample_count: u32,
    pub sample_sizes: Vec<u32>,
}

impl AtomData for SampleSizeData {
    const TYPE: FourCC = FourCC(0x7374737a);
}

impl ReadData for SampleSizeData {
    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 12];
        reader.read_exact(&mut buf)?;
        match BigEndian::read_u32(&buf[4..]) {
            0 => {
                let number_of_entries = BigEndian::read_u32(&buf[8..]);
                let mut buf = Vec::new();
                buf.resize(number_of_entries as usize * 4, 0);
                reader.read_exact(&mut buf)?;
                Ok(Self{
                    version: buf[0],
                    flags: buf[1..4].try_into().unwrap(),
                    constant_sample_size: 0,
                    sample_count: number_of_entries,
                    sample_sizes: (0..number_of_entries as usize).map(|i| BigEndian::read_u32(&buf[i * 4..])).collect(),
                })
            },
            constant_sample_size @ _ => Ok(Self{
                version: buf[0],
                flags: buf[1..4].try_into().unwrap(),
                constant_sample_size: constant_sample_size,
                sample_count: BigEndian::read_u32(&buf[8..]),
                sample_sizes: vec![],
            }),
        }
    }
}

impl WriteData for SampleSizeData {
    fn write<W: Write>(&self, mut writer: W) -> Result<()> {
        writer.write_u8(self.version)?;
        writer.write(&self.flags)?;
        writer.write_u32::<BigEndian>(self.constant_sample_size)?;
        if self.constant_sample_size == 0 {
            writer.write_u32::<BigEndian>(self.sample_sizes.len() as _)?;
            for entry in self.sample_sizes.iter() {
                entry.write(&mut writer)?;
            }
        } else {
            writer.write_u32::<BigEndian>(self.sample_count)?;
        }
        Ok(())
    }
}

impl SampleSizeData {
    // Returns the size of the given zero-based sample number.
    pub fn sample_size(&self, n: u64) -> Option<u32> {
        let n = n as usize;
        match self.constant_sample_size {
            0 => match n < self.sample_sizes.len() {
                true => Some(self.sample_sizes[n]),
                false => None,
            },
            _ => Some(self.constant_sample_size),
        }
    }

    pub fn iter_sample_sizes<'a>(&'a self) -> impl 'a + Iterator<Item=u32> {
        match self.constant_sample_size {
            0 => either::Left(self.sample_sizes.iter().copied()),
            _ => either::Right(std::iter::repeat(self.constant_sample_size)),
        }
    }

    // Returns a new version of the data for the given range of samples.
    pub fn trimmed(&self, start_sample: u64, sample_count: u64) -> Self {
        if self.constant_sample_size != 0 {
            let mut ret = self.clone();
            ret.sample_count = sample_count as _;
            ret
        } else {
            let mut ret = Self::default();
            let source_sample_count = self.sample_sizes.len() as u64;
            let start_sample = start_sample.min(source_sample_count);
            let end_sample = (start_sample + sample_count).min(source_sample_count);
            ret.sample_count = sample_count as _;
            ret.sample_sizes.extend_from_slice(&self.sample_sizes.as_slice()[start_sample as usize..end_sample as usize]);
            ret
        }
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct TimeToSampleDataEntry {
    pub sample_count: u32,
    pub sample_duration: u32,
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct TimeToSampleData {
    pub version: u8,
    pub flags: [u8; 3],
    pub entries: Vec<TimeToSampleDataEntry>
}

impl AtomData for TimeToSampleData {
    const TYPE: FourCC = FourCC(0x73747473);
}

impl TimeToSampleData {
    pub fn duration(&self) -> u64 {
        self.entries.iter().fold(0, |acc, e| acc + (e.sample_count as u64) * (e.sample_duration as u64))
    }

    pub fn sample_count(&self) -> u64 {
        self.entries.iter().fold(0, |acc, e| acc + e.sample_count as u64)
    }

    // Provides the time for the given zero-based sample.
    pub fn sample_time(&self, mut sample: u64) -> Option<u64> {
        let mut t = 0;
        for entry in self.entries.iter() {
            if (entry.sample_count as u64) <= sample {
                t += (entry.sample_count as u64) * (entry.sample_duration as u64);
                sample -= entry.sample_count as u64;
            } else {
                return Some(t + sample * (entry.sample_duration as u64));
            }
        }
        None
    }

    // Returns a new version of the data for the given range of samples.
    pub fn trimmed(&self, start_sample: u64, sample_count: u64) -> Self {
        let mut ret = Self::default();
        let mut n = 0;
        let end_sample = start_sample + sample_count;
        for entry in self.entries.iter() {
            if n >= end_sample {
                break;
            }
            let entry_end = n + entry.sample_count as u64;
            if entry_end > start_sample {
                ret.entries.push(TimeToSampleDataEntry{
                    sample_count: (entry_end.min(end_sample) - start_sample.max(n)) as u32,
                    sample_duration: entry.sample_duration,
                })
            }
            n = entry_end;
        }
        ret
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct SampleToChunkDataEntry {
    pub first_chunk: u32,
    pub samples_per_chunk: u32,
    pub sample_description_id: u32,
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct SampleToChunkData {
    pub version: u8,
    pub flags: [u8; 3],
    pub entries: Vec<SampleToChunkDataEntry>
}

impl AtomData for SampleToChunkData {
    const TYPE: FourCC = FourCC(0x73747363);
}

impl SampleToChunkData {
    // Returns the zero-based sample number that the given zero-based chunk number starts with.
    pub fn chunk_first_sample(&self, n: u32) -> u64 {
        if self.entries.len() == 1 {
            return (n as u64)*self.entries[0].samples_per_chunk as u64;
        }
        let mut sample_offset: u64 = 0;
        for i in 1..self.entries.len() {
            let e = &self.entries[i];
            let prev = &self.entries[i-1];
            if e.first_chunk > n {
                return sample_offset + ((n + 1 - prev.first_chunk) * prev.samples_per_chunk) as u64;
            }
            sample_offset += ((e.first_chunk - prev.first_chunk) as u64) * (prev.samples_per_chunk as u64);
        }
        let last = &self.entries[self.entries.len()-1];
        sample_offset + (n + 1 - last.first_chunk) as u64 * (last.samples_per_chunk as u64)
    }

    // Returns the zero-based chunk number that the given zero-based sample number is in.
    pub fn sample_chunk(&self, n: u64) -> u32 {
        if self.entries.len() == 1 {
            return (n as u32)/self.entries[0].samples_per_chunk;
        }
        let mut sample_offset: u64 = 0;
        for i in 1..self.entries.len() {
            let e = &self.entries[i];
            let prev = &self.entries[i-1];
            let new_sample_offset = sample_offset + ((e.first_chunk-prev.first_chunk) as u64) * (prev.samples_per_chunk as u64);
            if new_sample_offset as u64 > n {
                return prev.first_chunk - 1 + ((n-sample_offset)/(prev.samples_per_chunk as u64)) as u32;
            }
            sample_offset = new_sample_offset;
        }
        let last = &self.entries[self.entries.len()-1];
        last.first_chunk - 1 + ((n-sample_offset)/(last.samples_per_chunk as u64)) as u32
    }

    // Returns the number of samples.
    pub fn sample_count(&self, chunk_count: u32) -> u64 {
        let mut ret = 0;
        if !self.entries.is_empty() {
            for i in 1..self.entries.len() {
                let e = &self.entries[i-1];
                let next = &self.entries[i];
                ret += ((next.first_chunk - e.first_chunk) as u64) * (e.samples_per_chunk as u64);
            }
            let e = &self.entries[self.entries.len()-1];
            ret += (chunk_count - e.first_chunk + 1) as u64 * e.samples_per_chunk as u64;
        }
        ret
    }

    // Returns a Vec of zero-based chunk numbers for all samples, given a total sample count. The returned vector always has a length of exactly sample_count.
    pub fn sample_chunks(&self, sample_count: u64) -> Vec<u32> {
        let mut out = Vec::with_capacity(sample_count as _);
        if !self.entries.is_empty() {
            let mut sample_offset: u64 = 0;
            for i in 1..self.entries.len() {
                let e = &self.entries[i-1];
                let next = &self.entries[i];
                for chunk in e.first_chunk..next.first_chunk {
                    out.resize(out.len() + e.samples_per_chunk as usize, chunk - 1);
                }
                let entry_sample_count = ((next.first_chunk - e.first_chunk) as u64) * (e.samples_per_chunk as u64);
                sample_offset += entry_sample_count;
                if sample_offset > sample_count {
                    break;
                }
            }
            let e = &self.entries[self.entries.len()-1];
            let mut chunk = e.first_chunk - 1;
            while sample_count > sample_offset {
                out.resize(out.len() + (sample_count - sample_offset).min(e.samples_per_chunk as _) as usize, chunk);
                sample_offset += e.samples_per_chunk as u64;
                chunk += 1;
            }
        }
        out.resize(sample_count as _, 0);
        out
    }

    // Returns a new version of the data for the given range of samples. The output of this
    // function will contain exactly one chunk per entry.
    pub fn trimmed(&self, start_sample: u64, sample_count: u64) -> Self {
        let mut ret = Self::default();
        let mut sample_offset: u64 = 0;
        for i in 0..self.entries.len() {
            let e = &self.entries[i];
            let samples = if i + 1 < self.entries.len() {
                (self.entries[i + 1].first_chunk - e.first_chunk) as u64 * e.samples_per_chunk as u64
            } else {
                sample_count
            };
            let range_start = sample_offset.max(start_sample);
            let range_end = (sample_offset+samples).min(start_sample+sample_count);
            if range_end > range_start {
                ret.entries.push(SampleToChunkDataEntry{
                    first_chunk: (ret.entries.len() + 1) as u32,
                    samples_per_chunk: (range_end - range_start) as u32,
                    sample_description_id: e.sample_description_id,
                });
            }
            sample_offset += samples;
        }
        ret
    }
}

pub trait MediaType: fmt::Debug {
    type SampleDescriptionDataEntry: Clone + std::cmp::PartialEq + fmt::Debug + ReadData;
}

#[derive(Clone, Debug, PartialEq)]
pub struct GeneralMediaType;

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct GeneralSampleDescriptionDataEntry {
    pub data_format: u32,
    pub reserved: [u8; 6],
    pub data_reference_index: u16,
}

impl MediaType for GeneralMediaType {
    type SampleDescriptionDataEntry = GeneralSampleDescriptionDataEntry;
}

#[derive(Clone, Debug, PartialEq)]
pub struct VideoMediaType;

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct VideoSampleDescriptionDataEntry {
    pub data_format: u32,
    pub reserved: [u8; 6],
    pub data_reference_index: u16,
}

impl MediaType for VideoMediaType {
    type SampleDescriptionDataEntry = VideoSampleDescriptionDataEntry;
}

#[derive(Clone, Debug, PartialEq)]
pub struct VideoMediaInformationData {
    pub header: VideoMediaInformationHeaderData,
    pub handler_reference: HandlerReferenceData,
    pub sample_table: Option<SampleTableData<VideoMediaType>>,
}

impl AtomData for VideoMediaInformationData {
    const TYPE: FourCC = FourCC(0x6d696e66);
}

impl ReadData for VideoMediaInformationData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing video media information header"))?,
            handler_reference: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing video media information handler reference"))?,
            sample_table: read_one(&mut reader)?,
        })
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct VideoMediaInformationHeaderData {}

impl AtomData for VideoMediaInformationHeaderData {
    const TYPE: FourCC = FourCC(0x766d6864);
}

#[derive(Clone, Debug, PartialEq)]
pub struct SoundMediaType;

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct SoundSampleDescriptionDataEntry {
    pub data_format: u32,
    pub reserved: [u8; 6],
    pub data_reference_index: u16,
    pub version: u16,
    pub revision_level: u16,
    pub vendor: u32,
    pub number_of_channels: u16,
}

impl MediaType for SoundMediaType {
    type SampleDescriptionDataEntry = SoundSampleDescriptionDataEntry;
}

#[derive(Clone, Debug, PartialEq)]
pub struct SoundMediaInformationData {
    pub header: SoundMediaInformationHeaderData,
    pub handler_reference: HandlerReferenceData,
    pub sample_table: Option<SampleTableData<SoundMediaType>>,
}

impl AtomData for SoundMediaInformationData {
    const TYPE: FourCC = FourCC(0x6d696e66);
}

impl ReadData for SoundMediaInformationData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing sound media information header"))?,
            handler_reference: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing sound media information handler reference"))?,
            sample_table: read_one(&mut reader)?,
        })
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct SoundMediaInformationHeaderData {}

impl AtomData for SoundMediaInformationHeaderData {
    const TYPE: FourCC = FourCC(0x736D6864);
}

#[derive(Clone, Debug, PartialEq)]
pub struct BaseMediaInformationData {
    pub header: BaseMediaInformationHeaderData,
    pub sample_table: Option<SampleTableData<SoundMediaType>>,
}

impl AtomData for BaseMediaInformationData {
    const TYPE: FourCC = FourCC(0x6d696e66);
}

impl ReadData for BaseMediaInformationData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing base media information header"))?,
            sample_table: read_one(&mut reader)?,
        })
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct BaseMediaInformationHeaderData {}

impl AtomData for BaseMediaInformationHeaderData {
    const TYPE: FourCC = FourCC(0x676d6864);
}

#[derive(Clone, Debug, PartialEq)]
pub struct SampleTableData<M: MediaType> {
    pub sample_description: Option<SampleDescriptionData<M>>,
    pub chunk_offset: Option<ChunkOffsetData>,
    pub chunk_offset_64: Option<ChunkOffset64Data>,
    pub sample_size: Option<SampleSizeData>,
    pub sample_to_chunk: Option<SampleToChunkData>,
    pub time_to_sample: Option<TimeToSampleData>,
}

impl<M: MediaType> AtomData for SampleTableData<M> {
    const TYPE: FourCC = FourCC(0x7374626c);
}

impl<M: MediaType> ReadData for SampleTableData<M> {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            sample_description: read_one(&mut reader)?,
            chunk_offset: read_one(&mut reader)?,
            chunk_offset_64: read_one(&mut reader)?,
            sample_size: read_one(&mut reader)?,
            sample_to_chunk: read_one(&mut reader)?,
            time_to_sample: read_one(&mut reader)?,
        })
    }
}

impl<M: MediaType> SampleTableData<M> {
    // Returns the offset within the file of the given zero-based sample.
    pub fn sample_offset(&self, sample: u64) -> Option<u64> {
        let sample_to_chunk_data = self.sample_to_chunk.as_ref()?;
        let sample_size_data = self.sample_size.as_ref()?;

        let chunk = sample_to_chunk_data.sample_chunk(sample);
        let chunk_offset = self.chunk_offset.as_ref().and_then(|d| if (chunk as usize) < d.offsets.len() { Some(d.offsets[chunk as usize] as u64) } else { None });
        let chunk_offset_64 = self.chunk_offset_64.as_ref().and_then(|d| if (chunk as usize) < d.offsets.len() { Some(d.offsets[chunk as usize] as u64) } else { None });
        let chunk_offset = chunk_offset.or(chunk_offset_64)?;

        let mut offset_in_chunk = 0;
        for sample in sample_to_chunk_data.chunk_first_sample(chunk)..sample {
            offset_in_chunk += sample_size_data.sample_size(sample)? as u64;
        }
        Some(chunk_offset + offset_in_chunk)
    }

    pub fn iter_chunk_offsets<'a>(&'a self) -> Option<impl 'a + Iterator<Item=u64>> {
        if let Some(co) = &self.chunk_offset_64 {
            Some(either::Left(co.offsets.iter().copied()))
        } else if let Some(co) = &self.chunk_offset {
            Some(either::Right(co.offsets.iter().map(|&n| n as u64)))
        } else {
            None
        }
    }

    pub fn sample_count(&self) -> u64 {
        if let Some(sample_size) = self.sample_size.as_ref() {
            if sample_size.constant_sample_size == 0 || sample_size.sample_count != 0 {
                return sample_size.sample_count as u64;
            }
        }
        let chunk_count = if let Some(co) = &self.chunk_offset_64 {
            co.offsets.len() as u32
        } else if let Some(co) = &self.chunk_offset {
            co.offsets.len() as u32
        } else {
            return 0;
        };
        self.sample_to_chunk.as_ref().map(|v| v.sample_count(chunk_count)).unwrap_or(0)
    }

    pub fn sample_offsets(&self) -> Option<Vec<u64>> {
        let mut chunk_offsets: Vec<u64> = self.iter_chunk_offsets()?.collect();
        let sample_count = self.sample_count();
        let sample_chunks: Vec<u32> = self.sample_to_chunk.as_ref()?.sample_chunks(sample_count);
        let mut out = Vec::with_capacity(sample_count as usize);
        for (&chunk, size) in sample_chunks.iter().zip(self.sample_size.as_ref()?.iter_sample_sizes()) {
            out.push(chunk_offsets[chunk as usize]);
            chunk_offsets[chunk as usize] += size as u64;
        }
        Some(out)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct SampleDescriptionData<M: MediaType> {
    pub entries: Vec<M::SampleDescriptionDataEntry>,
}

impl<M: MediaType> ReadData for SampleDescriptionData<M> {
    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 8];
        reader.read_exact(&mut buf)?;
        let number_of_entries = BigEndian::read_u32(&buf[4..]);
        let mut entries = Vec::new();
        let mut size_prefix_buf = [0; 4];
        for _ in 0..number_of_entries {
            reader.read_exact(&mut size_prefix_buf)?;
            let size = BigEndian::read_u32(&size_prefix_buf);
            if size < 4 {
                break;
            }
            let mut buf = Vec::new();
            buf.resize((size - 4) as _, 0);
            reader.read_exact(&mut buf)?;
            entries.push(M::SampleDescriptionDataEntry::read(Cursor::new(buf.as_slice()))?);
        }
        Ok(Self{
            entries: entries,
        })
    }
}

impl<M: MediaType> AtomData for SampleDescriptionData<M> {
    const TYPE: FourCC = FourCC(0x73747364);
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct MetadataData {}

impl AtomData for MetadataData {
    const TYPE: FourCC = FourCC(0x6d657461);
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct TrackReferenceData {}

impl AtomData for TrackReferenceData {
    const TYPE: FourCC = FourCC(0x74726566);
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct ExtendedLanguageTagData {}

impl AtomData for ExtendedLanguageTagData {
    const TYPE: FourCC = FourCC(0x656c6e67);
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct DataInformationData {}

impl AtomData for DataInformationData {
    const TYPE: FourCC = FourCC(0x64696e66);
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct UserDataData {}

impl AtomData for UserDataData {
    const TYPE: FourCC = FourCC(0x75647461);
}

#[derive(Clone, Debug, PartialEq)]
pub struct EditData {
    pub edit_list: Option<EditListData>,
}

impl AtomData for EditData {
    const TYPE: FourCC = FourCC(0x65647473);
}

impl ReadData for EditData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            edit_list: read_one(&mut reader)?,
        })
    }
}

impl WriteData for EditData {
    fn write<W: Write>(&self, mut writer: W) -> Result<()> {
        if let Some(edit_list) = self.edit_list.as_ref() {
            writer.write_atom(edit_list.clone())?;
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct EditListDataEntry {
    pub track_duration: u32,
    pub media_time: i32,
    pub media_rate: FixedPoint32,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct EditListData {
    pub version: u8,
    pub flags: [u8; 3],
    pub entries: Vec<EditListDataEntry>,
}

impl AtomData for EditListData {
    const TYPE: FourCC = FourCC(0x656c7374);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sample_table_data() {
        let table = SampleTableData::<VideoMediaType>{
            sample_description: None,
            time_to_sample: None,
            chunk_offset: Some(ChunkOffsetData{
                version: 0,
                flags: [0; 3],
                offsets: vec![40, 623924, 1247864, 1865576, 2489396, 3107180, 3731028, 4348828, 4972704],
            }),
            chunk_offset_64: None,
            sample_size: Some(SampleSizeData{
                version: 0,
                flags: [9, 85, 12],
                constant_sample_size: 0,
                sample_count: 9,
                sample_sizes: vec![611596, 611652, 611568, 611532, 611640, 611560, 611656, 611588, 611544],
            }),
            sample_to_chunk: Some(SampleToChunkData{
                version: 0,
                flags: [0; 3],
                entries: vec![SampleToChunkDataEntry{
                    first_chunk: 1,
                    samples_per_chunk: 1,
                    sample_description_id: 1,
                }],
            }),
        };

        assert_eq!(vec![40, 623924, 1247864, 1865576, 2489396, 3107180, 3731028, 4348828, 4972704], table.iter_chunk_offsets().unwrap().collect::<Vec<u64>>());
        assert_eq!(vec![40, 623924, 1247864, 1865576, 2489396, 3107180, 3731028, 4348828, 4972704], table.sample_offsets().unwrap());

        let table = SampleTableData::<SoundMediaType>{
            sample_description: None,
            time_to_sample: None,
            chunk_offset: Some(ChunkOffsetData{
                version: 0,
                flags: [0; 3],
                offsets: vec![611636, 1235576, 1859432, 2477108, 3101036, 3718740, 4342684, 4960416, 5584248],
            }),
            chunk_offset_64: None,
            sample_size: Some(SampleSizeData{
                version: 0,
                flags: [0; 3],
                constant_sample_size: 6,
                sample_count: 2048+2048+1024+2048+1024+2048+1024+2048+2048,
                sample_sizes: vec![],
            }),
            sample_to_chunk: Some(SampleToChunkData{
                version: 0,
                flags: [0; 3],
                entries: vec![SampleToChunkDataEntry{
                    first_chunk: 1,
                    samples_per_chunk: 2048,
                    sample_description_id: 1,
                }, SampleToChunkDataEntry{
                    first_chunk: 3,
                    samples_per_chunk: 1024,
                    sample_description_id: 1,
                }, SampleToChunkDataEntry {
                    first_chunk: 4,
                    samples_per_chunk: 2048,
                    sample_description_id: 1,
                }, SampleToChunkDataEntry{
                    first_chunk: 5,
                    samples_per_chunk: 1024,
                    sample_description_id: 1,
                }, SampleToChunkDataEntry{
                    first_chunk: 6,
                    samples_per_chunk: 2048,
                    sample_description_id: 1,
                }, SampleToChunkDataEntry{
                    first_chunk: 7,
                    samples_per_chunk: 1024,
                    sample_description_id: 1,
                }, SampleToChunkDataEntry{
                    first_chunk: 8,
                    samples_per_chunk: 2048,
                    sample_description_id: 1,
                }],
            }),
        };

        assert_eq!(vec![611636, 1235576, 1859432, 2477108, 3101036, 3718740, 4342684, 4960416, 5584248], table.iter_chunk_offsets().unwrap().collect::<Vec<u64>>());
        assert_eq!(2048+2048+1024+2048+1024+2048+1024+2048+2048, table.sample_count());
        assert_eq!(table.sample_count(), table.sample_offsets().unwrap().len() as u64);
    }

    #[test]
    fn test_sample_size_data() {
        let data = SampleSizeData{
            version: 0,
            flags: [0; 3],
            constant_sample_size: 0,
            sample_count: 4,
            sample_sizes: vec![0, 1, 2, 3],
        };

        assert_eq!(vec![0, 1], data.trimmed(0, 2).sample_sizes);
        assert_eq!(vec![1, 2, 3], data.trimmed(1, 3).sample_sizes);
    }

    #[test]
    fn test_time_to_sample_data() {
        let data = TimeToSampleData{
            version: 0,
            flags: [0; 3],
            entries: vec![
                TimeToSampleDataEntry{
                    sample_count: 8,
                    sample_duration: 1,
                },
                TimeToSampleDataEntry{
                    sample_count: 20,
                    sample_duration: 2,
                },
            ],
        };

        assert_eq!(vec![
            TimeToSampleDataEntry{
                sample_count: 2,
                sample_duration: 1,
            },
        ], data.trimmed(0, 2).entries);

        assert_eq!(vec![
            TimeToSampleDataEntry{
                sample_count: 4,
                sample_duration: 1,
            },
        ], data.trimmed(2, 4).entries);

        assert_eq!(vec![
            TimeToSampleDataEntry{
                sample_count: 6,
                sample_duration: 1,
            },
            TimeToSampleDataEntry{
                sample_count: 2,
                sample_duration: 2,
            },
        ], data.trimmed(2, 8).entries);
    }

    #[test]
    fn test_sample_to_chunk_data() {
        let data = SampleToChunkData{
            version: 0,
            flags: [0; 3],
            entries: vec![SampleToChunkDataEntry{
                first_chunk: 1,
                samples_per_chunk: 2048,
                sample_description_id: 1,
            }, SampleToChunkDataEntry{
                first_chunk: 3,
                samples_per_chunk: 1024,
                sample_description_id: 1,
            }, SampleToChunkDataEntry {
                first_chunk: 4,
                samples_per_chunk: 2048,
                sample_description_id: 1,
            }, SampleToChunkDataEntry{
                first_chunk: 5,
                samples_per_chunk: 1024,
                sample_description_id: 1,
            }, SampleToChunkDataEntry{
                first_chunk: 6,
                samples_per_chunk: 2048,
                sample_description_id: 1,
            }, SampleToChunkDataEntry{
                first_chunk: 7,
                samples_per_chunk: 1024,
                sample_description_id: 1,
            }, SampleToChunkDataEntry{
                first_chunk: 8,
                samples_per_chunk: 2048,
                sample_description_id: 1,
            }],
        };

        let chunks = data.sample_chunks(15360);
        assert_eq!(3, chunks[5120]);
        assert_eq!(8, chunks[15359]);

        let data = SampleToChunkData{
            version: 0,
            flags: [0; 3],
            entries: vec![
                SampleToChunkDataEntry{
                    first_chunk: 1,
                    samples_per_chunk: 4,
                    sample_description_id: 1,
                },
                SampleToChunkDataEntry{
                    first_chunk: 3,
                    samples_per_chunk: 5,
                    sample_description_id: 2,
                },
            ],
        };

        assert_eq!(vec![0, 0, 0, 0, 1, 1, 1, 1, 2, 2], data.sample_chunks(10));

        assert_eq!(vec![
            SampleToChunkDataEntry{
                first_chunk: 1,
                samples_per_chunk: 2,
                sample_description_id: 1,
            },
        ], data.trimmed(0, 2).entries);

        assert_eq!(vec![
            SampleToChunkDataEntry{
                first_chunk: 1,
                samples_per_chunk: 4,
                sample_description_id: 1,
            },
        ], data.trimmed(2, 4).entries);

        assert_eq!(vec![
            SampleToChunkDataEntry{
                first_chunk: 1,
                samples_per_chunk: 6,
                sample_description_id: 1,
            },
            SampleToChunkDataEntry{
                first_chunk: 2,
                samples_per_chunk: 2,
                sample_description_id: 2,
            },
        ], data.trimmed(2, 8).entries);
    }
}
