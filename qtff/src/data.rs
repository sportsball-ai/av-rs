use super::atom::{Atom, AtomReader, AtomWriteExt, FourCC, SectionReader};
use super::deserializer::Deserializer;
use super::error::{Error, Result};
use super::serializer::Serializer;

use std::collections::HashMap;
use std::convert::TryInto;
use std::fmt;
use std::io::{Cursor, Read, Seek, SeekFrom, Write};

use byteorder::{BigEndian, ByteOrder, ReadBytesExt, WriteBytesExt};
use serde::{de, ser};

pub fn read_one<T: AtomData, R: Read + Seek>(mut reader: R) -> Result<Option<T>> {
    reader.seek(SeekFrom::Start(0))?;
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
    reader.seek(SeekFrom::Start(0))?;
    let atoms = AtomReader::new(&mut reader)
        .filter(|a| match a {
            Ok(a) => a.typ == T::TYPE,
            Err(_) => true,
        })
        .map(|r| r.map_err(|e| e.into()))
        .collect::<Result<Vec<Atom>>>()?;
    atoms.iter().map(|a| T::read(a.data(&mut reader))).collect()
}

pub trait ReadData: Sized {
    fn read<R: Read + Seek>(reader: R) -> Result<Self>;
}

pub fn read<T: ReadData, R: Read + Seek>(reader: R) -> Result<T> {
    ReadData::read(reader)
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

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FixedPoint16(f32);

impl ser::Serialize for FixedPoint16 {
    fn serialize<S: ser::Serializer>(&self, serializer: S) -> std::result::Result<S::Ok, S::Error> {
        serializer.serialize_u16((self.0 * (0x100 as f32)) as _)
    }
}

impl<'de> de::Deserialize<'de> for FixedPoint16 {
    fn deserialize<D: de::Deserializer<'de>>(deserializer: D) -> std::result::Result<FixedPoint16, D::Error> {
        Ok(((u16::deserialize(deserializer)? as f32) / (0x100 as f32)).into())
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

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FixedPoint32(f32);

impl ser::Serialize for FixedPoint32 {
    fn serialize<S: ser::Serializer>(&self, serializer: S) -> std::result::Result<S::Ok, S::Error> {
        serializer.serialize_u32((self.0 * (0x10000 as f32)) as _)
    }
}

impl<'de> de::Deserialize<'de> for FixedPoint32 {
    fn deserialize<D: de::Deserializer<'de>>(deserializer: D) -> std::result::Result<FixedPoint32, D::Error> {
        Ok(((u32::deserialize(deserializer)? as f32) / (0x10000 as f32)).into())
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
    pub metadata: Option<MetadataData>,
}

impl AtomData for MovieData {
    const TYPE: FourCC = FourCC(0x6d6f6f76);
}

impl ReadData for MovieData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing movie header"))?,
            tracks: read_all(&mut reader)?,
            metadata: read_one(&mut reader)?,
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
    pub metadata: Option<MetadataData>,
}

impl AtomData for TrackData {
    const TYPE: FourCC = FourCC(0x7472616b);
}

impl ReadData for TrackData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing track header"))?,
            media: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing track media"))?,
            edit: read_one(&mut reader)?,
            metadata: read_one(&mut reader)?,
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
    Timecode(BaseMediaInformationData<TimecodeMediaType>),
    Base(BaseMediaInformationData<GeneralMediaType>),
}

#[derive(Clone, Debug, PartialEq)]
pub struct MediaData {
    pub header: MediaHeaderData,
    pub handler_reference: Option<HandlerReferenceData>,
    pub information: Option<MediaInformationData>,
    pub metadata: Option<MetadataData>,
}

impl AtomData for MediaData {
    const TYPE: FourCC = FourCC(0x6d646961);
}

impl ReadData for MediaData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        let handler_reference: Option<HandlerReferenceData> = read_one(&mut reader)?;
        Ok(Self {
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media header"))?,
            information: {
                let component_subtype = handler_reference
                    .as_ref()
                    .and_then(|v| v.component_subtype.to_string())
                    .unwrap_or("".to_string());
                match component_subtype.as_str() {
                    "vide" => Some(MediaInformationData::Video(
                        read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media video information"))?,
                    )),
                    "soun" => Some(MediaInformationData::Sound(
                        read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media sound information"))?,
                    )),
                    "tmcd" => Some(MediaInformationData::Timecode(
                        read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media timecode information"))?,
                    )),
                    _ => Some(MediaInformationData::Base(
                        read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media base information"))?,
                    )),
                }
            },
            handler_reference: handler_reference,
            metadata: read_one(&mut reader)?,
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
                let mut entry_buf = Vec::new();
                entry_buf.resize(number_of_entries as usize * 4, 0);
                reader.read_exact(&mut entry_buf)?;
                Ok(Self {
                    version: buf[0],
                    flags: buf[1..4].try_into().unwrap(),
                    constant_sample_size: 0,
                    sample_count: number_of_entries,
                    sample_sizes: (0..number_of_entries as usize).map(|i| BigEndian::read_u32(&entry_buf[i * 4..])).collect(),
                })
            }
            constant_sample_size @ _ => Ok(Self {
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

    pub fn iter_sample_sizes<'a>(&'a self) -> impl 'a + Iterator<Item = u32> {
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
            ret.sample_sizes
                .extend_from_slice(&self.sample_sizes.as_slice()[start_sample as usize..end_sample as usize]);
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
    pub entries: Vec<TimeToSampleDataEntry>,
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
        if sample == 0 {
            Some(t)
        } else {
            None
        }
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
                ret.entries.push(TimeToSampleDataEntry {
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
    pub entries: Vec<SampleToChunkDataEntry>,
}

impl AtomData for SampleToChunkData {
    const TYPE: FourCC = FourCC(0x73747363);
}

#[derive(Clone, Debug)]
pub struct SampleChunkInfo {
    // The zero-based chunk number.
    pub number: u32,

    pub first_sample: u64,
    pub samples: u64,

    pub entry: usize,
    pub entry_first_sample: u64,

    // The zero-based sample description.
    pub sample_description: u32,
}

impl SampleToChunkData {
    // Returns the zero-based sample number that the given zero-based chunk number starts with.
    pub fn chunk_first_sample(&self, n: u32) -> u64 {
        if self.entries.len() == 1 {
            return (n as u64) * self.entries[0].samples_per_chunk as u64;
        }
        let mut sample_offset: u64 = 0;
        for i in 1..self.entries.len() {
            let e = &self.entries[i];
            let prev = &self.entries[i - 1];
            if e.first_chunk > n {
                return sample_offset + ((n + 1 - prev.first_chunk) * prev.samples_per_chunk) as u64;
            }
            sample_offset += ((e.first_chunk - prev.first_chunk) as u64) * (prev.samples_per_chunk as u64);
        }
        let last = &self.entries[self.entries.len() - 1];
        sample_offset + (n + 1 - last.first_chunk) as u64 * (last.samples_per_chunk as u64)
    }

    // Returns the zero-based chunk number that the given zero-based sample number is in.
    pub fn sample_chunk(&self, n: u64) -> u32 {
        self.sample_chunk_info(n, None).number
    }

    // Returns info for the chunk that the given zero-based sample number is in. If you provide the
    // results of an invocation for a sample preceeding n, you can ammortize the cost of iterating
    // the chunk info entries across multiple invocations.
    pub fn sample_chunk_info(&self, n: u64, prev: Option<&SampleChunkInfo>) -> SampleChunkInfo {
        if self.entries.len() == 1 {
            let e = &self.entries[0];
            return SampleChunkInfo {
                number: (n / e.samples_per_chunk as u64) as _,
                first_sample: (n / e.samples_per_chunk as u64) * e.samples_per_chunk as u64,
                samples: e.samples_per_chunk as u64,
                entry: 0,
                entry_first_sample: 0,
                sample_description: e.sample_description_id - 1,
            };
        }
        let mut sample_offset: u64 = prev.as_ref().map(|prev| prev.entry_first_sample).unwrap_or(0);
        for i in prev.map(|prev| prev.entry + 1).unwrap_or(1) as usize..self.entries.len() {
            let e = &self.entries[i];
            let prev = &self.entries[i - 1];
            let new_sample_offset = sample_offset + ((e.first_chunk - prev.first_chunk) as u64) * (prev.samples_per_chunk as u64);
            if new_sample_offset as u64 > n {
                let relative = ((n - sample_offset) / (prev.samples_per_chunk as u64)) as u32;
                return SampleChunkInfo {
                    number: prev.first_chunk - 1 + relative,
                    first_sample: sample_offset + relative as u64 * prev.samples_per_chunk as u64,
                    samples: prev.samples_per_chunk as _,
                    entry: i - 1,
                    entry_first_sample: sample_offset,
                    sample_description: prev.sample_description_id - 1,
                };
            }
            sample_offset = new_sample_offset;
        }
        let last = &self.entries[self.entries.len() - 1];
        let relative = ((n - sample_offset) / (last.samples_per_chunk as u64)) as u32;
        SampleChunkInfo {
            number: last.first_chunk - 1 + relative,
            first_sample: sample_offset + relative as u64 * last.samples_per_chunk as u64,
            samples: last.samples_per_chunk as _,
            entry: self.entries.len() - 1,
            entry_first_sample: sample_offset,
            sample_description: last.sample_description_id - 1,
        }
    }

    // Returns the number of samples.
    pub fn sample_count(&self, chunk_count: u32) -> u64 {
        let mut ret = 0;
        if !self.entries.is_empty() {
            for i in 1..self.entries.len() {
                let e = &self.entries[i - 1];
                let next = &self.entries[i];
                ret += ((next.first_chunk - e.first_chunk) as u64) * (e.samples_per_chunk as u64);
            }
            let e = &self.entries[self.entries.len() - 1];
            ret += (chunk_count - e.first_chunk + 1) as u64 * e.samples_per_chunk as u64;
        }
        ret
    }

    // Returns a new version of the data for the given range of samples. The chunks emitted by this
    // function will be a subset of the original chunks. I.e. each emitted chunk will correspond to
    // exactly one of the original chunks.
    pub fn trimmed(&self, start_sample: u64, sample_count: u64) -> Self {
        let mut ret = Self::default();
        let end_sample = start_sample + sample_count;
        let mut entry_start_sample: u64 = 0;
        let mut next_chunk = 1;
        for i in 0..self.entries.len() {
            if entry_start_sample >= end_sample {
                break;
            }

            let e = &self.entries[i];

            let entry_sample_count = if i + 1 < self.entries.len() {
                (self.entries[i + 1].first_chunk - e.first_chunk) as u64 * e.samples_per_chunk as u64
            } else {
                end_sample - entry_start_sample
            };
            let entry_end_sample = entry_start_sample + entry_sample_count;

            let mut overlap_start_sample = start_sample.max(entry_start_sample);
            let overlap_end_sample = end_sample.min(entry_end_sample);

            if overlap_end_sample > overlap_start_sample {
                // If the overlap doesn't begin on a chunk boundary, add an entry for the first partial chunk.
                let partial_chunk_skip = (overlap_start_sample - entry_start_sample) % e.samples_per_chunk as u64;
                if partial_chunk_skip != 0 {
                    let samples = (e.samples_per_chunk as u64 - partial_chunk_skip).min(overlap_end_sample - overlap_start_sample) as u32;
                    ret.entries.push(SampleToChunkDataEntry {
                        first_chunk: next_chunk,
                        samples_per_chunk: samples,
                        sample_description_id: e.sample_description_id,
                    });
                    next_chunk += 1;
                    overlap_start_sample += samples as u64;
                }

                if overlap_end_sample >= overlap_start_sample + e.samples_per_chunk as u64 {
                    // Add an entry for all of the full chunks.
                    ret.entries.push(SampleToChunkDataEntry {
                        first_chunk: next_chunk,
                        samples_per_chunk: e.samples_per_chunk,
                        sample_description_id: e.sample_description_id,
                    });
                    let chunks = (overlap_end_sample - overlap_start_sample) / e.samples_per_chunk as u64;
                    next_chunk += chunks as u32;
                    overlap_start_sample += chunks * e.samples_per_chunk as u64;
                }

                if overlap_end_sample > overlap_start_sample {
                    // Add an entry for the last partial chunk.
                    ret.entries.push(SampleToChunkDataEntry {
                        first_chunk: next_chunk,
                        samples_per_chunk: (overlap_end_sample - overlap_start_sample) as _,
                        sample_description_id: e.sample_description_id,
                    });
                    next_chunk += 1;
                }
            }

            entry_start_sample = entry_end_sample;
        }
        ret
    }
}

pub trait MediaType: fmt::Debug {
    type SampleDescriptionDataEntry: Clone + std::cmp::PartialEq + fmt::Debug + ReadData;

    fn constant_sample_size(_desc: &Self::SampleDescriptionDataEntry) -> Option<u32> {
        None
    }
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

#[derive(Clone, Debug, PartialEq)]
pub struct VideoSampleDescriptionDataEntry {
    pub data_format: u32,
    pub reserved: [u8; 6],
    pub data_reference_index: u16,
    pub version: u16,
    pub revision_level: u16,
    pub vendor: u32,
    pub temporal_quality: u32,
    pub spatial_quality: u32,
    pub width: u16,
    pub height: u16,
    pub horizontal_resolution: FixedPoint32,
    pub vertical_resolution: FixedPoint32,
    pub data_size: u32,
    pub frame_count: u16,
    pub compressor_name: [u8; 32],
    pub depth: u16,
    pub color_table_id: i16,

    pub extensions: VideoSampleDescriptionDataEntryExtensions,
}

impl ReadData for VideoSampleDescriptionDataEntry {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            data_format: read(&mut reader)?,
            reserved: read(&mut reader)?,
            data_reference_index: read(&mut reader)?,
            version: read(&mut reader)?,
            revision_level: read(&mut reader)?,
            vendor: read(&mut reader)?,
            temporal_quality: read(&mut reader)?,
            spatial_quality: read(&mut reader)?,
            width: read(&mut reader)?,
            height: read(&mut reader)?,
            horizontal_resolution: read(&mut reader)?,
            vertical_resolution: read(&mut reader)?,
            data_size: read(&mut reader)?,
            frame_count: read(&mut reader)?,
            compressor_name: read(&mut reader)?,
            depth: read(&mut reader)?,
            color_table_id: read(&mut reader)?,
            extensions: {
                let begin = reader.seek(SeekFrom::Current(0))? as usize;
                let end = reader.seek(SeekFrom::End(0))? as usize;
                read(SectionReader::new(&mut reader, begin, end - begin))?
            },
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct VideoSampleDescriptionDataEntryExtensions {
    pub avc_decoder_configuration: Option<AVCDecoderConfigurationData>,
    pub hvc_decoder_configuration: Option<HVCDecoderConfigurationData>,
}

impl ReadData for VideoSampleDescriptionDataEntryExtensions {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            avc_decoder_configuration: read_one(&mut reader)?,
            hvc_decoder_configuration: read_one(&mut reader)?,
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct AVCDecoderConfigurationData {
    pub record: Vec<u8>,
}

impl AtomData for AVCDecoderConfigurationData {
    const TYPE: FourCC = FourCC(0x61766343);
}

impl ReadData for AVCDecoderConfigurationData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        let mut record = Vec::new();
        reader.read_to_end(&mut record)?;
        Ok(Self { record })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct HVCDecoderConfigurationData {
    pub record: Vec<u8>,
}

impl AtomData for HVCDecoderConfigurationData {
    const TYPE: FourCC = FourCC(0x68766343);
}

impl ReadData for HVCDecoderConfigurationData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        let mut record = Vec::new();
        reader.read_to_end(&mut record)?;
        Ok(Self { record })
    }
}

impl MediaType for VideoMediaType {
    type SampleDescriptionDataEntry = VideoSampleDescriptionDataEntry;
}

#[derive(Clone, Debug, PartialEq)]
pub struct VideoMediaInformationData {
    pub header: VideoMediaInformationHeaderData,
    pub handler_reference: Option<HandlerReferenceData>, // required for qtff, optional for mp4
    pub sample_table: Option<SampleTableData<VideoMediaType>>,
}

impl AtomData for VideoMediaInformationData {
    const TYPE: FourCC = FourCC(0x6d696e66);
}

impl ReadData for VideoMediaInformationData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing video media information header"))?,
            handler_reference: read_one(&mut reader)?,
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

#[derive(Clone, Debug, PartialEq)]
pub struct ElementaryStreamDescriptorData {
    pub version: u32,
    pub descriptor: Vec<u8>,
}

impl AtomData for ElementaryStreamDescriptorData {
    const TYPE: FourCC = FourCC(0x65736473);
}

impl ReadData for ElementaryStreamDescriptorData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            version: read(&mut reader)?,
            descriptor: {
                let mut descriptor = Vec::new();
                reader.read_to_end(&mut descriptor)?;
                descriptor
            },
        })
    }
}

#[derive(Clone, Default, Debug, PartialEq)]
pub struct SoundSampleDescriptionDataEntryExtensions {
    pub elementary_stream_descriptor: Option<ElementaryStreamDescriptorData>,
}

impl ReadData for SoundSampleDescriptionDataEntryExtensions {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            elementary_stream_descriptor: read_one(&mut reader)?,
        })
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct SoundSampleDescriptionDataEntryV0 {
    pub revision_level: u16,
    pub vendor: u32,
    pub number_of_channels: u16,
    pub sample_size: u16,
    pub compression_id: u16,
    pub packet_size: u16,
    pub sample_rate: FixedPoint32,

    pub extensions: SoundSampleDescriptionDataEntryExtensions,
}

impl ReadData for SoundSampleDescriptionDataEntryV0 {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            revision_level: read(&mut reader)?,
            vendor: read(&mut reader)?,
            number_of_channels: read(&mut reader)?,
            sample_size: read(&mut reader)?,
            compression_id: read(&mut reader)?,
            packet_size: read(&mut reader)?,
            sample_rate: read(&mut reader)?,
            extensions: {
                let begin = reader.seek(SeekFrom::Current(0))? as usize;
                let end = reader.seek(SeekFrom::End(0))? as usize;
                read(SectionReader::new(&mut reader, begin, end - begin))?
            },
        })
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct SoundSampleDescriptionDataEntryV1 {
    pub revision_level: u16,
    pub vendor: u32,
    pub number_of_channels: u16,
    pub sample_size: u16,
    pub compression_id: u16,
    pub packet_size: u16,
    pub sample_rate: FixedPoint32,
    pub samples_per_packet: u32,
    pub bytes_per_packet: u32,
    pub bytes_per_frame: u32,
    pub bytes_per_sample: u32,

    pub extensions: SoundSampleDescriptionDataEntryExtensions,
}

impl ReadData for SoundSampleDescriptionDataEntryV1 {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            revision_level: read(&mut reader)?,
            vendor: read(&mut reader)?,
            number_of_channels: read(&mut reader)?,
            sample_size: read(&mut reader)?,
            compression_id: read(&mut reader)?,
            packet_size: read(&mut reader)?,
            sample_rate: read(&mut reader)?,
            samples_per_packet: read(&mut reader)?,
            bytes_per_packet: read(&mut reader)?,
            bytes_per_frame: read(&mut reader)?,
            bytes_per_sample: read(&mut reader)?,
            extensions: {
                let begin = reader.seek(SeekFrom::Current(0))? as usize;
                let end = reader.seek(SeekFrom::End(0))? as usize;
                read(SectionReader::new(&mut reader, begin, end - begin))?
            },
        })
    }
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq)]
pub struct SoundSampleDescriptionDataEntryV2 {
    // TODO: add v2 fields. the docs i'm looking at right now seem to be confused about whether v2
// appends new fields to v1 or replaces fields in v1 :-/
}

#[derive(Clone, Debug, PartialEq)]
pub enum SoundSampleDescriptionDataEntryVersion {
    V0(SoundSampleDescriptionDataEntryV0),
    V1(SoundSampleDescriptionDataEntryV1),
    V2(SoundSampleDescriptionDataEntryV2),
}

impl ReadData for SoundSampleDescriptionDataEntryVersion {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(match reader.read_u16::<BigEndian>()? {
            0 => Self::V0(read(&mut reader)?),
            1 => Self::V1(read(&mut reader)?),
            _ => Self::V2(read(&mut reader)?),
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct SoundSampleDescriptionDataEntry {
    pub data_format: u32,
    pub reserved: [u8; 6],
    pub data_reference_index: u16,
    pub version: SoundSampleDescriptionDataEntryVersion,
}

impl ReadData for SoundSampleDescriptionDataEntry {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            data_format: read(&mut reader)?,
            reserved: read(&mut reader)?,
            data_reference_index: read(&mut reader)?,
            version: read(&mut reader)?,
        })
    }
}

impl MediaType for SoundMediaType {
    type SampleDescriptionDataEntry = SoundSampleDescriptionDataEntry;

    // For sound, the constant sample size in the description takes precedence over the size found in the stsz atom.
    fn constant_sample_size(desc: &Self::SampleDescriptionDataEntry) -> Option<u32> {
        (match &desc.version {
            SoundSampleDescriptionDataEntryVersion::V0(v) => Some(((v.sample_size / 8) * v.number_of_channels) as u32),
            SoundSampleDescriptionDataEntryVersion::V1(v) => Some(v.bytes_per_frame as _),
            SoundSampleDescriptionDataEntryVersion::V2(_) => None,
        })
        .filter(|&size| size > 0)
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct SoundMediaInformationData {
    pub header: SoundMediaInformationHeaderData,
    pub handler_reference: Option<HandlerReferenceData>, // required for qtff, optional for mp4
    pub sample_table: Option<SampleTableData<SoundMediaType>>,
}

impl AtomData for SoundMediaInformationData {
    const TYPE: FourCC = FourCC(0x6d696e66);
}

impl ReadData for SoundMediaInformationData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing sound media information header"))?,
            handler_reference: read_one(&mut reader)?,
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
pub struct TimecodeMediaType;

pub enum TimecodeSampleDescriptionFlags {
    DropFrame = 0x0001,
    WrapsAt24Hours = 0x0002,
    NegativeTimesOk = 0x0004,
    Counter = 0x0008,
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct TimecodeSampleDescriptionDataEntry {
    pub data_format: u32,
    pub reserved: [u8; 6],
    pub data_reference_index: u16,
    pub reserved2: u32,
    pub flags: u32,
    pub time_scale: u32,
    pub frame_duration: u32,
    pub number_of_frames: u8,
    pub reserved3: u8,
}

#[derive(Clone, Debug, PartialEq)]
pub enum TimecodeSample {
    Counter(u32),
    Timecode(Timecode),
}

impl TimecodeSample {
    pub fn data(&self) -> u32 {
        match self {
            Self::Counter(ticks) => *ticks,
            Self::Timecode(tc) => {
                ((tc.hours as u32) << 24)
                    | (if tc.negative { 0x800000 } else { 0 })
                    | ((tc.minutes as u32) << 16)
                    | ((tc.seconds as u32) << 8)
                    | tc.frames as u32
            }
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Timecode {
    pub negative: bool,
    pub hours: u8,
    pub minutes: u8,
    pub seconds: u8,
    pub frames: u8,
}

impl Timecode {
    pub fn frame_number(&self, fps: f64) -> i64 {
        let abs_number = if fps.round() != fps {
            let minutes = self.hours as u64 * 60 + self.minutes as u64;
            let ten_minutes = minutes / 10;
            let rem_minutes = minutes % 10;
            let fp10m = (fps * 600.0).round() as u64;
            let fps = ((fp10m as f64) / 600.0).ceil() as u64;
            let dropped_per_minute = (fps * 600 - fp10m) / 9;
            ten_minutes * fp10m + rem_minutes * (fps * 60 - dropped_per_minute) + self.seconds as u64 * fps + self.frames as u64
        } else {
            ((self.hours as u64 * 60 + self.minutes as u64) * 60 + self.seconds as u64) * fps as u64 + self.frames as u64
        };
        if self.negative {
            -(abs_number as i64)
        } else {
            abs_number as i64
        }
    }

    pub fn from_frame_number(n: i64, fps: f64) -> Timecode {
        let abs_number = n.abs() as u64;
        if fps.round() != fps {
            let fp10m = (fps * 600.0).round() as u64;
            let ten_minutes = abs_number / fp10m;
            let abs_number = abs_number % fp10m;

            let fps = ((fp10m as f64) / 600.0).ceil() as u64;
            let dropped_per_minute = (fps * 600 - fp10m) / 9;
            let fpm = fps * 60 - dropped_per_minute;
            let minutes = if abs_number > dropped_per_minute {
                (abs_number - dropped_per_minute) / fpm
            } else {
                abs_number / fpm
            };
            let abs_number = if minutes == 0 {
                abs_number
            } else {
                (abs_number - dropped_per_minute) % fpm + dropped_per_minute
            };

            Timecode {
                negative: n < 0,
                hours: (ten_minutes / 6) as _,
                minutes: ((ten_minutes % 6) * 10 + minutes) as _,
                seconds: (abs_number / fps) as _,
                frames: (abs_number % fps) as _,
            }
        } else {
            let fps = fps as u64;
            let fpm = fps * 60;
            let fph = fpm * 60;
            Timecode {
                negative: n < 0,
                hours: (abs_number / fph) as _,
                minutes: ((abs_number / fpm) % 60) as _,
                seconds: ((abs_number / fps) % 60) as _,
                frames: (abs_number % fps) as _,
            }
        }
    }
}

impl TimecodeSampleDescriptionDataEntry {
    pub fn parse_sample_data(&self, data: u32) -> TimecodeSample {
        // Samples seem to always be counters instead of timecode records despite what the docs
        // say. FFMpeg makes the same assumption:
        // https://github.com/FFmpeg/FFmpeg/blob/dc1c3c640d245bc3e7a7a4c82ae1a6d06343abab/libavformat/mov.c#L7229
        TimecodeSample::Counter(data)
    }

    pub fn fps(&self) -> f64 {
        self.time_scale as f64 / self.frame_duration as f64
    }

    pub fn add_frames_to_sample(&self, sample: TimecodeSample, frames: i64) -> TimecodeSample {
        match sample {
            TimecodeSample::Counter(ticks) => {
                let wrap = if (self.flags & TimecodeSampleDescriptionFlags::WrapsAt24Hours as u32) != 0 {
                    self.time_scale as i64 * 60 * 60 * 24 * self.number_of_frames as i64 / self.frame_duration as i64
                } else {
                    0xffffffff
                };
                if (self.flags & TimecodeSampleDescriptionFlags::Counter as u32) != 0 {
                    TimecodeSample::Counter(((ticks as i64 + (frames / self.number_of_frames as i64)) % wrap) as u32)
                } else {
                    TimecodeSample::Counter(((ticks as i64 + frames) % wrap) as u32)
                }
            }
            TimecodeSample::Timecode(tc) => {
                let fps = self.fps();
                let mut new_frame_number = tc.frame_number(fps) + frames;
                if (self.flags & TimecodeSampleDescriptionFlags::WrapsAt24Hours as u32) != 0 {
                    new_frame_number = new_frame_number % (fps * 60.0 * 60.0 * 24.0).round() as i64;
                }
                TimecodeSample::Timecode(Timecode::from_frame_number(new_frame_number, fps))
            }
        }
    }
}

impl MediaType for TimecodeMediaType {
    type SampleDescriptionDataEntry = TimecodeSampleDescriptionDataEntry;
}

#[derive(Clone, Debug, PartialEq)]
pub struct BaseMediaInformationData<M: MediaType> {
    pub header: BaseMediaInformationHeaderData,
    pub sample_table: Option<SampleTableData<M>>,
}

impl<M: MediaType> AtomData for BaseMediaInformationData<M> {
    const TYPE: FourCC = FourCC(0x6d696e66);
}

impl<M: MediaType> ReadData for BaseMediaInformationData<M> {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
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

impl<M: MediaType> Default for SampleTableData<M> {
    fn default() -> Self {
        Self {
            sample_description: None,
            chunk_offset: None,
            chunk_offset_64: None,
            sample_size: None,
            sample_to_chunk: None,
            time_to_sample: None,
        }
    }
}

impl<M: MediaType> AtomData for SampleTableData<M> {
    const TYPE: FourCC = FourCC(0x7374626c);
}

impl<M: MediaType> ReadData for SampleTableData<M> {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
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
    pub fn sample_offset(&self, sample: u64, chunk_info: &SampleChunkInfo) -> Option<u64> {
        let chunk = chunk_info.number;
        let chunk_offset = self.chunk_offset.as_ref().and_then(|d| {
            if (chunk as usize) < d.offsets.len() {
                Some(d.offsets[chunk as usize] as u64)
            } else {
                None
            }
        });
        let chunk_offset_64 = self.chunk_offset_64.as_ref().and_then(|d| {
            if (chunk as usize) < d.offsets.len() {
                Some(d.offsets[chunk as usize] as u64)
            } else {
                None
            }
        });
        let chunk_offset = chunk_offset.or(chunk_offset_64)?;

        let sample_description = self.sample_description(chunk_info.sample_description)?;

        let sample_size = self.sample_size.as_ref()?;
        let constant_sample_size = M::constant_sample_size(&sample_description).or(if sample_size.constant_sample_size > 0 {
            Some(sample_size.constant_sample_size)
        } else {
            None
        });

        let offset_in_chunk = match constant_sample_size {
            Some(size) => size as u64 * (sample - chunk_info.first_sample),
            _ => {
                if chunk_info.first_sample <= sample && (sample as usize) < sample_size.sample_sizes.len() {
                    sample_size.sample_sizes[(chunk_info.first_sample as usize)..(sample as usize)]
                        .iter()
                        .fold(0, |acc, &x| acc + x as u64)
                } else {
                    return None;
                }
            }
        };

        Some(chunk_offset + offset_in_chunk)
    }

    pub fn sample_size(&self, sample: u64, chunk_info: &SampleChunkInfo) -> Option<u32> {
        let sample_description = self.sample_description(chunk_info.sample_description)?;

        let sample_size = self.sample_size.as_ref()?;
        let constant_sample_size = M::constant_sample_size(&sample_description).or(if sample_size.constant_sample_size > 0 {
            Some(sample_size.constant_sample_size)
        } else {
            None
        });

        constant_sample_size.or_else(|| {
            if (sample as usize) < sample_size.sample_sizes.len() {
                Some(sample_size.sample_sizes[sample as usize])
            } else {
                None
            }
        })
    }

    // Returns info for the chunk that the given zero-based sample number is in. If you provide the
    // results of an invocation for a sample preceeding n, you can ammortize the cost of iterating
    // the chunk info entries across multiple invocations.
    pub fn sample_chunk_info(&self, sample: u64, hint: Option<&SampleChunkInfo>) -> Option<SampleChunkInfo> {
        Some(self.sample_to_chunk.as_ref()?.sample_chunk_info(sample, hint))
    }

    pub fn sample_description<'a>(&'a self, id: u32) -> Option<&'a M::SampleDescriptionDataEntry> {
        let sample_descriptions = &self.sample_description.as_ref()?.entries;
        if sample_descriptions.len() <= id as usize {
            return None;
        }
        Some(&sample_descriptions[id as usize])
    }

    pub fn iter_chunk_offsets<'a>(&'a self) -> Option<impl 'a + Iterator<Item = u64>> {
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
        Ok(Self { entries: entries })
    }
}

impl<M: MediaType> AtomData for SampleDescriptionData<M> {
    const TYPE: FourCC = FourCC(0x73747364);
}

#[derive(Clone, Debug, PartialEq)]
pub struct MetadataData {
    pub handler: MetadataHandlerData,
    pub item_keys: MetadataItemKeysData,
    pub item_list: MetadataItemListData,
}

impl AtomData for MetadataData {
    const TYPE: FourCC = FourCC(0x6d657461);
}

impl ReadData for MetadataData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            handler: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing metadata handler"))?,
            item_keys: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing metadata item keys"))?,
            item_list: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing metadata item list"))?,
        })
    }
}

impl MetadataData {
    // Returns metadata as a hashmap.
    pub fn metadata(&self) -> HashMap<String, &Vec<MetadataValueData>> {
        let mut ret = HashMap::new();
        for (key_index, item) in self.item_list.items.iter() {
            let key_index = *key_index as usize;
            if key_index >= self.item_keys.entries.len() + 1 {
                continue;
            }
            if let Some(key) = String::from_utf8(self.item_keys.entries[key_index - 1].key_value.clone()).ok() {
                ret.insert(key, &item.values);
            }
        }
        return ret;
    }
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct MetadataHandlerData {
    pub version: u8,
    pub flags: [u8; 3],
    pub predefined: u32,
    pub handler_type: FourCC,
}

impl AtomData for MetadataHandlerData {
    const TYPE: FourCC = FourCC(0x68646c72);
}

#[derive(Clone, Debug, PartialEq)]
pub struct MetadataItemKeysDataEntry {
    pub key_namespace: u32,
    pub key_value: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct MetadataItemKeysData {
    pub entries: Vec<MetadataItemKeysDataEntry>,
}

impl AtomData for MetadataItemKeysData {
    const TYPE: FourCC = FourCC(0x6b657973);
}

impl ReadData for MetadataItemKeysData {
    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 8];
        reader.read_exact(&mut buf)?;
        let entry_count = BigEndian::read_u32(&buf[4..]);
        let mut entries = vec![];
        for _ in 0..entry_count {
            reader.read_exact(&mut buf)?;
            let key_size = BigEndian::read_u32(&buf);
            if key_size < 8 {
                return Err(Error::MalformedFile("invalid metadata key size"));
            }
            let key_value_size = (key_size - 8) as usize;
            let mut key_value = Vec::with_capacity(key_value_size);
            key_value.resize(key_value_size, 0);
            reader.read_exact(&mut key_value)?;
            entries.push(MetadataItemKeysDataEntry {
                key_namespace: BigEndian::read_u32(&buf[4..]),
                key_value: key_value,
            })
        }
        Ok(Self { entries: entries })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum MetadataValue {
    F32(f32),
    F64(f64),
    I8(i8),
    I16(i16),
    I32(i32),
    I64(i64),
    U8(u8),
    U16(u16),
    U32(u32),
    U64(u64),
    String(String),
    Point { x: f32, y: f32 },
    Dimensions { width: f32, height: f32 },
    Rect { x: f32, y: f32, width: f32, height: f32 },
    Unknown { type_indicator: u32, data: Vec<u8> },
}

#[derive(Clone, Debug, PartialEq)]
pub struct MetadataValueData {
    pub locale: u32,
    pub value: MetadataValue,
}

impl ReadData for MetadataValueData {
    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut header_buf = [0; 8];
        reader.read_exact(&mut header_buf)?;

        Ok(Self {
            locale: BigEndian::read_u32(&header_buf[4..]),
            value: match BigEndian::read_u32(&header_buf) {
                1 => {
                    let mut data = vec![];
                    reader.read_to_end(&mut data)?;
                    MetadataValue::String(String::from_utf8(data).map_err(|_| Error::MalformedFile("malformed utf-8 string"))?)
                }
                23 => MetadataValue::F32(reader.read_f32::<BigEndian>()?),
                24 => MetadataValue::F64(reader.read_f64::<BigEndian>()?),
                65 => MetadataValue::I8(reader.read_i8()?),
                66 => MetadataValue::I16(reader.read_i16::<BigEndian>()?),
                67 => MetadataValue::I32(reader.read_i32::<BigEndian>()?),
                70 => MetadataValue::Point {
                    x: reader.read_f32::<BigEndian>()?,
                    y: reader.read_f32::<BigEndian>()?,
                },
                71 => MetadataValue::Dimensions {
                    width: reader.read_f32::<BigEndian>()?,
                    height: reader.read_f32::<BigEndian>()?,
                },
                72 => MetadataValue::Rect {
                    x: reader.read_f32::<BigEndian>()?,
                    y: reader.read_f32::<BigEndian>()?,
                    width: reader.read_f32::<BigEndian>()?,
                    height: reader.read_f32::<BigEndian>()?,
                },
                74 => MetadataValue::I64(reader.read_i64::<BigEndian>()?),
                75 => MetadataValue::U8(reader.read_u8()?),
                76 => MetadataValue::U16(reader.read_u16::<BigEndian>()?),
                77 => MetadataValue::U32(reader.read_u32::<BigEndian>()?),
                78 => MetadataValue::U64(reader.read_u64::<BigEndian>()?),
                n @ _ => {
                    let mut data = vec![];
                    reader.read_to_end(&mut data)?;
                    MetadataValue::Unknown { type_indicator: n, data: data }
                }
            },
        })
    }
}

impl AtomData for MetadataValueData {
    const TYPE: FourCC = FourCC(0x64617461);
}

#[derive(Clone, Debug, PartialEq)]
pub struct MetadataItemData {
    pub values: Vec<MetadataValueData>,
}

impl ReadData for MetadataItemData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self {
            values: read_all(&mut reader)?,
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct MetadataItemListData {
    pub items: Vec<(u32, MetadataItemData)>,
}

impl AtomData for MetadataItemListData {
    const TYPE: FourCC = FourCC(0x696c7374);
}

impl ReadData for MetadataItemListData {
    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        let atoms = AtomReader::new(&mut reader).map(|r| r.map_err(|e| e.into())).collect::<Result<Vec<Atom>>>()?;
        Ok(Self {
            items: atoms
                .iter()
                .map(|a| Ok((a.typ.0, MetadataItemData::read(a.data(&mut reader))?)))
                .collect::<Result<_>>()?,
        })
    }
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
        Ok(Self {
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
    use std::io::Cursor;

    #[test]
    fn test_video_sample_description_data_entry() {
        let buf = vec![
            0x61, 0x76, 0x63, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x04, 0x38, 0x00, 0x48, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x36, 0x61, 0x76, 0x63, 0x43, 0x01, 0x64, 0x00, 0x29, 0xFF, 0xE1,
            0x00, 0x19, 0x67, 0x64, 0x00, 0x29, 0xAC, 0x2C, 0xA5, 0x01, 0xE0, 0x11, 0x1F, 0x73, 0x50, 0x10, 0x10, 0x14, 0x00, 0x00, 0x0F, 0xA4, 0x00, 0x03,
            0xA9, 0x82, 0x10, 0x01, 0x00, 0x06, 0x68, 0xE8, 0x81, 0x13, 0x52, 0x50, 0xFD, 0xF8, 0xF8, 0x00,
        ];
        let entry = VideoSampleDescriptionDataEntry::read(Cursor::new(&buf)).unwrap();
        assert_eq!(
            VideoSampleDescriptionDataEntry {
                data_format: 1635148593,
                reserved: [0; 6],
                data_reference_index: 1,
                version: 0,
                revision_level: 0,
                vendor: 0,
                temporal_quality: 0,
                spatial_quality: 0,
                width: 1920,
                height: 1080,
                horizontal_resolution: 72.0.into(),
                vertical_resolution: 72.0.into(),
                data_size: 0,
                frame_count: 1,
                compressor_name: [0; 32],
                depth: 24,
                color_table_id: -1,
                extensions: VideoSampleDescriptionDataEntryExtensions {
                    avc_decoder_configuration: Some(AVCDecoderConfigurationData {
                        record: vec![
                            0x01, 0x64, 0x00, 0x29, 0xFF, 0xE1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x29, 0xAC, 0x2C, 0xA5, 0x01, 0xE0, 0x11, 0x1F, 0x73, 0x50, 0x10,
                            0x10, 0x14, 0x00, 0x00, 0x0F, 0xA4, 0x00, 0x03, 0xA9, 0x82, 0x10, 0x01, 0x00, 0x06, 0x68, 0xE8, 0x81, 0x13, 0x52, 0x50, 0xFD, 0xF8,
                            0xF8, 0x00
                        ]
                    }),
                    hvc_decoder_configuration: None,
                },
            },
            entry
        );
    }

    #[test]
    fn test_video_sample_description_data_entry_with_terminating_zeros() {
        // "Some video sample descriptions contain an optional 4-byte terminator with all bytes set
        // to 0, following all other sample description and sample description extension data."
        let buf = vec![
            0x61, 0x70, 0x63, 0x73, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x61, 0x70, 0x70, 0x6c, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x03, 0xff, 0x10, 0x00, 0x08, 0x70, 0x00, 0x48, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x15, 0x41,
            0x70, 0x70, 0x6c, 0x65, 0x20, 0x50, 0x72, 0x6f, 0x52, 0x65, 0x73, 0x20, 0x34, 0x32, 0x32, 0x20, 0x28, 0x4c, 0x54, 0x29, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xff, 0xff, 0x00, 0x00, 0x00, 0x0a, 0x66, 0x69, 0x65, 0x6c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x12,
            0x63, 0x6f, 0x6c, 0x72, 0x6e, 0x63, 0x6c, 0x63, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x70, 0x61, 0x73, 0x70, 0x00, 0x00,
            0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        ];
        VideoSampleDescriptionDataEntry::read(Cursor::new(&buf)).unwrap();
    }

    #[test]
    fn test_sound_media_type() {
        let desc = SoundSampleDescriptionDataEntry {
            data_format: 1836069985,
            reserved: [0; 6],
            data_reference_index: 1,
            version: SoundSampleDescriptionDataEntryVersion::V1(SoundSampleDescriptionDataEntryV1 {
                revision_level: 0,
                vendor: 0,
                number_of_channels: 2,
                sample_size: 16,
                compression_id: 65534,
                packet_size: 0,
                sample_rate: 48000.0.into(),
                samples_per_packet: 1024,
                bytes_per_packet: 0,
                bytes_per_frame: 0,
                bytes_per_sample: 2,
                extensions: SoundSampleDescriptionDataEntryExtensions {
                    elementary_stream_descriptor: None,
                },
            }),
        };
        assert_eq!(None, SoundMediaType::constant_sample_size(&desc));
    }

    #[test]
    fn test_sample_table_data() {
        let table = SampleTableData::<VideoMediaType> {
            sample_description: None,
            time_to_sample: None,
            chunk_offset: Some(ChunkOffsetData {
                version: 0,
                flags: [0; 3],
                offsets: vec![40, 623924, 1247864, 1865576, 2489396, 3107180, 3731028, 4348828, 4972704],
            }),
            chunk_offset_64: None,
            sample_size: Some(SampleSizeData {
                version: 0,
                flags: [9, 85, 12],
                constant_sample_size: 0,
                sample_count: 9,
                sample_sizes: vec![611596, 611652, 611568, 611532, 611640, 611560, 611656, 611588, 611544],
            }),
            sample_to_chunk: Some(SampleToChunkData {
                version: 0,
                flags: [0; 3],
                entries: vec![SampleToChunkDataEntry {
                    first_chunk: 1,
                    samples_per_chunk: 1,
                    sample_description_id: 1,
                }],
            }),
        };

        assert_eq!(
            vec![40, 623924, 1247864, 1865576, 2489396, 3107180, 3731028, 4348828, 4972704],
            table.iter_chunk_offsets().unwrap().collect::<Vec<u64>>()
        );

        let table = SampleTableData::<SoundMediaType> {
            sample_description: None,
            time_to_sample: None,
            chunk_offset: Some(ChunkOffsetData {
                version: 0,
                flags: [0; 3],
                offsets: vec![611636, 1235576, 1859432, 2477108, 3101036, 3718740, 4342684, 4960416, 5584248],
            }),
            chunk_offset_64: None,
            sample_size: Some(SampleSizeData {
                version: 0,
                flags: [0; 3],
                constant_sample_size: 6,
                sample_count: 2048 + 2048 + 1024 + 2048 + 1024 + 2048 + 1024 + 2048 + 2048,
                sample_sizes: vec![],
            }),
            sample_to_chunk: Some(SampleToChunkData {
                version: 0,
                flags: [0; 3],
                entries: vec![
                    SampleToChunkDataEntry {
                        first_chunk: 1,
                        samples_per_chunk: 2048,
                        sample_description_id: 1,
                    },
                    SampleToChunkDataEntry {
                        first_chunk: 3,
                        samples_per_chunk: 1024,
                        sample_description_id: 1,
                    },
                    SampleToChunkDataEntry {
                        first_chunk: 4,
                        samples_per_chunk: 2048,
                        sample_description_id: 1,
                    },
                    SampleToChunkDataEntry {
                        first_chunk: 5,
                        samples_per_chunk: 1024,
                        sample_description_id: 1,
                    },
                    SampleToChunkDataEntry {
                        first_chunk: 6,
                        samples_per_chunk: 2048,
                        sample_description_id: 1,
                    },
                    SampleToChunkDataEntry {
                        first_chunk: 7,
                        samples_per_chunk: 1024,
                        sample_description_id: 1,
                    },
                    SampleToChunkDataEntry {
                        first_chunk: 8,
                        samples_per_chunk: 2048,
                        sample_description_id: 1,
                    },
                ],
            }),
        };

        assert_eq!(
            vec![611636, 1235576, 1859432, 2477108, 3101036, 3718740, 4342684, 4960416, 5584248],
            table.iter_chunk_offsets().unwrap().collect::<Vec<u64>>()
        );
        assert_eq!(2048 + 2048 + 1024 + 2048 + 1024 + 2048 + 1024 + 2048 + 2048, table.sample_count());
    }

    #[test]
    fn test_sample_size_data() {
        let data = SampleSizeData {
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
        let data = TimeToSampleData {
            version: 0,
            flags: [0; 3],
            entries: vec![
                TimeToSampleDataEntry {
                    sample_count: 8,
                    sample_duration: 1,
                },
                TimeToSampleDataEntry {
                    sample_count: 20,
                    sample_duration: 2,
                },
            ],
        };

        assert_eq!(
            vec![TimeToSampleDataEntry {
                sample_count: 2,
                sample_duration: 1,
            },],
            data.trimmed(0, 2).entries
        );

        assert_eq!(
            vec![TimeToSampleDataEntry {
                sample_count: 4,
                sample_duration: 1,
            },],
            data.trimmed(2, 4).entries
        );

        assert_eq!(
            vec![
                TimeToSampleDataEntry {
                    sample_count: 6,
                    sample_duration: 1,
                },
                TimeToSampleDataEntry {
                    sample_count: 2,
                    sample_duration: 2,
                },
            ],
            data.trimmed(2, 8).entries
        );
    }

    #[test]
    fn test_sample_to_chunk_data() {
        let data = SampleToChunkData {
            version: 0,
            flags: [0; 3],
            entries: vec![
                SampleToChunkDataEntry {
                    first_chunk: 1,
                    samples_per_chunk: 4,
                    sample_description_id: 1,
                },
                SampleToChunkDataEntry {
                    first_chunk: 3,
                    samples_per_chunk: 5,
                    sample_description_id: 2,
                },
            ],
        };

        assert_eq!(
            vec![SampleToChunkDataEntry {
                first_chunk: 1,
                samples_per_chunk: 2,
                sample_description_id: 1,
            },],
            data.trimmed(0, 2).entries
        );

        assert_eq!(
            vec![
                SampleToChunkDataEntry {
                    first_chunk: 1,
                    samples_per_chunk: 2,
                    sample_description_id: 1,
                },
                SampleToChunkDataEntry {
                    first_chunk: 2,
                    samples_per_chunk: 2,
                    sample_description_id: 1,
                },
            ],
            data.trimmed(2, 4).entries
        );

        assert_eq!(
            vec![
                SampleToChunkDataEntry {
                    first_chunk: 1,
                    samples_per_chunk: 2,
                    sample_description_id: 1,
                },
                SampleToChunkDataEntry {
                    first_chunk: 2,
                    samples_per_chunk: 4,
                    sample_description_id: 1,
                },
                SampleToChunkDataEntry {
                    first_chunk: 3,
                    samples_per_chunk: 2,
                    sample_description_id: 2,
                },
            ],
            data.trimmed(2, 8).entries
        );
    }

    #[test]
    fn test_timecode() {
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 9,
                seconds: 59,
                frames: 28
            },
            Timecode::from_frame_number(17980, 29.97)
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 10,
                seconds: 0,
                frames: 0
            },
            Timecode::from_frame_number(17982, 29.97)
        );

        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 0,
                seconds: 0,
                frames: 0
            }
            .frame_number(29.97),
            0
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 0,
                seconds: 0,
                frames: 1
            }
            .frame_number(29.97),
            1
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 0,
                seconds: 1,
                frames: 1
            }
            .frame_number(29.97),
            31
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 1,
                seconds: 0,
                frames: 2
            }
            .frame_number(29.97),
            60 * 30
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 10,
                seconds: 0,
                frames: 0
            }
            .frame_number(29.97),
            17982
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 1,
                minutes: 11,
                seconds: 0,
                frames: 2
            }
            .frame_number(29.97),
            7 * 17982 + 60 * 30
        );

        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 10,
                seconds: 0,
                frames: 0
            },
            Timecode::from_frame_number(10 * 60 * 24, 24.0)
        );

        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 0,
                seconds: 0,
                frames: 0
            }
            .frame_number(24.0),
            0
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 0,
                seconds: 0,
                frames: 1
            }
            .frame_number(24.0),
            1
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 0,
                seconds: 1,
                frames: 1
            }
            .frame_number(24.0),
            25
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 1,
                seconds: 0,
                frames: 2
            }
            .frame_number(24.0),
            60 * 24 + 2
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 0,
                minutes: 10,
                seconds: 0,
                frames: 0
            }
            .frame_number(24.0),
            10 * 60 * 24
        );
        assert_eq!(
            Timecode {
                negative: false,
                hours: 1,
                minutes: 11,
                seconds: 0,
                frames: 2
            }
            .frame_number(24.0),
            71 * 60 * 24 + 2
        );

        for frame in -300000..300000 {
            let tc = Timecode::from_frame_number(frame, 29.97);
            assert_eq!(frame, tc.frame_number(29.97));

            let tc = Timecode::from_frame_number(frame, 24.0);
            assert_eq!(frame, tc.frame_number(24.0));
        }
    }
}
