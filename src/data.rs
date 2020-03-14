use super::atom::{Atom, AtomReader, FourCC};
use super::error::{Error, Result};

use std::io::{Read, Seek};

use byteorder::{BigEndian, ByteOrder};

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

pub fn read_fixed_point(buf: &[u8]) -> f32 {
    BigEndian::read_u32(buf) as f32 / (0x10000 as f32)
}

pub trait Data {
    fn read<R: Read>(reader: R) -> Result<Self>
        where Self: Sized
    ;
}

pub trait AtomData {
    const TYPE: FourCC;

    fn read<R: Read + Seek>(reader: R) -> Result<Self>
        where Self: Sized
    ;
}

#[derive(Clone)]
pub struct MovieData {
    pub tracks: Vec<TrackData>,
}

impl AtomData for MovieData {
    const TYPE: FourCC = FourCC(0x6d6f6f76);

    fn read<R: Read + Seek>(reader: R) -> Result<Self> {
        Ok(Self{
            tracks: read_all(reader)?,
        })
    }
}

#[derive(Clone)]
pub struct TrackData {
    pub header: TrackHeaderData,
    pub media: MediaData,
}

impl AtomData for TrackData {
    const TYPE: FourCC = FourCC(0x7472616b);

    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing track header"))?,
            media: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing track media"))?,
        })
    }
}

#[derive(Clone)]
pub struct TrackHeaderData {
    pub creation_time: u32,
    pub modification_time: u32,
    pub id: u32,
    pub duration: u32,
    pub width: f32,
    pub height: f32,
}

impl AtomData for TrackHeaderData {
    const TYPE: FourCC = FourCC(0x746b6864);

    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 84];
        reader.read_exact(&mut buf)?;
        Ok(Self{
            creation_time: BigEndian::read_u32(&buf[4..]),
            modification_time: BigEndian::read_u32(&buf[8..]),
            id: BigEndian::read_u32(&buf[12..]),
            duration: BigEndian::read_u32(&buf[20..]),
            width: read_fixed_point(&buf[76..]),
            height: read_fixed_point(&buf[80..]),
        })
    }
}

#[derive(Clone)]
pub enum MediaInformationData {
    Sound(SoundMediaInformationData),
    Video(VideoMediaInformationData),
}

#[derive(Clone)]
pub struct MediaData {
    pub header: MediaHeaderData,
    pub handler_reference: Option<HandlerReferenceData>,
    pub information: Option<MediaInformationData>,
}

impl AtomData for MediaData {
    const TYPE: FourCC = FourCC(0x6d646961);

    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        let handler_reference: Option<HandlerReferenceData> = read_one(&mut reader)?;
        Ok(Self{
            header: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media header"))?,
            information: {
                let component_subtype = handler_reference.as_ref().and_then(|v| v.component_subtype.to_string()).unwrap_or("".to_string());
                match component_subtype.as_str() {
                    "vide" => Some(MediaInformationData::Video(read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media video information"))?)),
                    "soun" => Some(MediaInformationData::Sound(read_one(&mut reader)?.ok_or(Error::MalformedFile("missing media sound information"))?)),
                    _ => None,
                }
            },
            handler_reference: handler_reference,
        })
    }
}

#[derive(Clone)]
pub struct MediaHeaderData {
    pub time_scale: u32,
    pub duration: u32,
}

impl AtomData for MediaHeaderData {
    const TYPE: FourCC = FourCC(0x6d646864);

    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 24];
        reader.read_exact(&mut buf)?;
        Ok(Self{
            time_scale: BigEndian::read_u32(&buf[12..]),
            duration: BigEndian::read_u32(&buf[16..]),
        })
    }
}

#[derive(Clone)]
pub struct HandlerReferenceData {
    pub component_type: FourCC,
    pub component_subtype: FourCC,
}

impl AtomData for HandlerReferenceData {
    const TYPE: FourCC = FourCC(0x68646c72);

    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 24];
        reader.read_exact(&mut buf)?;
        Ok(Self{
            component_type: FourCC(BigEndian::read_u32(&buf[4..])),
            component_subtype: FourCC(BigEndian::read_u32(&buf[8..])),
        })
    }
}

#[derive(Clone)]
pub struct ChunkOffset64Data {
    pub offsets: Vec<u64>,
}

impl AtomData for ChunkOffset64Data {
    const TYPE: FourCC = FourCC(0x636f3634);

    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 8];
        reader.read_exact(&mut buf)?;
        let number_of_entries = BigEndian::read_u32(&buf[4..]);
        let mut buf = Vec::new();
        buf.resize(number_of_entries as usize * 8, 0);
        reader.read_exact(&mut buf)?;
        let offsets = (0..number_of_entries as usize).map(|i| BigEndian::read_u64(&buf[i * 8..])).collect();
        Ok(Self{
            offsets: offsets,
        })
    }
}

#[derive(Clone)]
pub struct ChunkOffsetData {
    pub offsets: Vec<u32>,
}

impl AtomData for ChunkOffsetData {
    const TYPE: FourCC = FourCC(0x7374636f);

    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 8];
        reader.read_exact(&mut buf)?;
        let number_of_entries = BigEndian::read_u32(&buf[4..]);
        let mut buf = Vec::new();
        buf.resize(number_of_entries as usize * 4, 0);
        reader.read_exact(&mut buf)?;
        let offsets = (0..number_of_entries as usize).map(|i| BigEndian::read_u32(&buf[i * 4..])).collect();
        Ok(Self{
            offsets: offsets,
        })
    }
}

#[derive(Clone)]
pub struct SampleSizeData {
    pub constant_sample_size: u32,
    pub sample_sizes: Vec<u32>,
}

impl AtomData for SampleSizeData {
    const TYPE: FourCC = FourCC(0x7374737a);

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
                    constant_sample_size: 0,
                    sample_sizes: (0..number_of_entries as usize).map(|i| BigEndian::read_u32(&buf[i * 4..])).collect(),
                })
            },
            constant_sample_size @ _ => Ok(Self{
                constant_sample_size: constant_sample_size,
                sample_sizes: vec![],
            }),
        }
    }
}

impl SampleSizeData {
    // Returns the size of the given zero-based sample number.
    pub fn sample_size(&self, n: u64) -> Option<usize> {
        let n = n as usize;
        match self.constant_sample_size {
            0 => match n < self.sample_sizes.len() {
                true => Some(self.sample_sizes[n] as usize),
                false => None,
            },
            _ => Some(self.constant_sample_size as usize),
        }
    }
}

#[derive(Clone)]
pub struct TimeToSampleDataEntry {
    pub sample_count: u32,
    pub sample_duration: u32,
}

#[derive(Clone)]
pub struct TimeToSampleData {
    pub entries: Vec<TimeToSampleDataEntry>
}

impl AtomData for TimeToSampleData {
    const TYPE: FourCC = FourCC(0x73747473);

    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 8];
        reader.read_exact(&mut buf)?;
        let number_of_entries = BigEndian::read_u32(&buf[4..]);
        let mut buf = Vec::new();
        buf.resize(number_of_entries as usize * 8, 0);
        reader.read_exact(&mut buf)?;
        let entries = (0..number_of_entries as usize).map(|i| TimeToSampleDataEntry{
            sample_count: BigEndian::read_u32(&buf[i * 8..]),
            sample_duration: BigEndian::read_u32(&buf[i * 8 + 4..]),
        }).collect();
        Ok(Self{
            entries: entries,
        })
    }
}

impl TimeToSampleData {
    pub fn sample_count(&self) -> u64 {
        self.entries.iter().fold(0, |acc, e| acc + e.sample_count as u64)
    }
}

#[derive(Clone)]
pub struct SampleToChunkDataEntry {
    pub first_chunk: u32,
    pub samples_per_chunk: u32,
}

#[derive(Clone)]
pub struct SampleToChunkData {
    pub entries: Vec<SampleToChunkDataEntry>
}

impl AtomData for SampleToChunkData {
    const TYPE: FourCC = FourCC(0x73747363);

    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 8];
        reader.read_exact(&mut buf)?;
        let number_of_entries = BigEndian::read_u32(&buf[4..]);
        let mut buf = Vec::new();
        buf.resize(number_of_entries as usize * 12, 0);
        reader.read_exact(&mut buf)?;
        let entries = (0..number_of_entries as usize).map(|i| SampleToChunkDataEntry{
            first_chunk: BigEndian::read_u32(&buf[i * 12..]),
            samples_per_chunk: BigEndian::read_u32(&buf[i * 12 + 4..]),
        }).collect();
        Ok(Self{
            entries: entries,
        })
    }
}

impl SampleToChunkData {
    // Returns the zero-based sample number that the given zero-based chunk number starts with.
    pub fn chunk_first_sample(&self, n: u64) -> u64 {
        if self.entries.len() == 1 {
            return n*self.entries[0].samples_per_chunk as u64;
        }
        let mut sample_offset: u64 = 0;
        for i in 1..self.entries.len() {
            let e = &self.entries[i];
            let prev = &self.entries[i-1];
            if e.first_chunk as u64 > n {
                return sample_offset + (n + 1 - prev.first_chunk as u64) * (prev.samples_per_chunk as u64);
            }
            sample_offset += ((e.first_chunk - prev.first_chunk) as u64) * (prev.samples_per_chunk as u64);
        }
        let last = &self.entries[self.entries.len()-1];
        sample_offset + (n + 1 - last.first_chunk as u64) * (last.samples_per_chunk as u64)
    }

    // Returns the zero-based chunk number that the given zero-based sample number is in.
    pub fn sample_chunk(&self, n: u64) -> u64 {
        if self.entries.len() == 1 {
            return n/self.entries[0].samples_per_chunk as u64;
        }
        let mut sample_offset: u64 = 0;
        for i in 1..self.entries.len() {
            let e = &self.entries[i];
            let prev = &self.entries[i-1];
            let new_sample_offset = sample_offset + ((e.first_chunk-prev.first_chunk) as u64) * (prev.samples_per_chunk as u64);
            if new_sample_offset as u64 > n {
                return (prev.first_chunk as u64) - 1 + (n-sample_offset)/(prev.samples_per_chunk as u64);
            }
            sample_offset = new_sample_offset;
        }
        let last = &self.entries[self.entries.len()-1];
        last.first_chunk as u64 - 1 + (n-sample_offset)/(last.samples_per_chunk as u64)
    }
}

pub trait MediaType {
    type SampleDescriptionDataEntry: Clone + Data;
}

#[derive(Clone)]
pub struct VideoMediaType;

#[derive(Clone)]
pub struct VideoSampleDescriptionDataEntry {
    pub data_format: u32,
}

impl Data for VideoSampleDescriptionDataEntry {
    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 4];
        reader.read_exact(&mut buf)?;
        Ok(Self{
            data_format: BigEndian::read_u32(&buf),
        })
    }
}

impl MediaType for VideoMediaType {
    type SampleDescriptionDataEntry = VideoSampleDescriptionDataEntry;
}

#[derive(Clone)]
pub struct VideoMediaInformationData {
    pub handler_reference: HandlerReferenceData,
    pub sample_table: Option<SampleTableData<VideoMediaType>>,
}

impl AtomData for VideoMediaInformationData {
    const TYPE: FourCC = FourCC(0x6d696e66);

    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            handler_reference: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing video media information handler reference"))?,
            sample_table: read_one(&mut reader)?,
        })
    }
}

#[derive(Clone)]
pub struct SoundMediaType;

#[derive(Clone)]
pub struct SoundSampleDescriptionDataEntry {
    pub data_format: u32,
    pub number_of_channels: u16,
}

impl Data for SoundSampleDescriptionDataEntry {
    fn read<R: Read>(mut reader: R) -> Result<Self> {
        let mut buf = [0; 22];
        reader.read_exact(&mut buf)?;
        Ok(Self{
            data_format: BigEndian::read_u32(&buf),
            number_of_channels: BigEndian::read_u16(&buf[20..]),
        })
    }
}

impl MediaType for SoundMediaType {
    type SampleDescriptionDataEntry = SoundSampleDescriptionDataEntry;
}

#[derive(Clone)]
pub struct SoundMediaInformationData {
    pub handler_reference: HandlerReferenceData,
    pub sample_table: Option<SampleTableData<SoundMediaType>>,
}

impl AtomData for SoundMediaInformationData {
    const TYPE: FourCC = FourCC(0x6d696e66);

    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            handler_reference: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing sound media information handler reference"))?,
            sample_table: read_one(&mut reader)?,
        })
    }
}

#[derive(Clone)]
pub struct SampleTableData<M: MediaType> {
    pub sample_description: SampleDescriptionData<M>,
    pub chunk_offset: Option<ChunkOffsetData>,
    pub chunk_offset_64: Option<ChunkOffset64Data>,
    pub sample_size: Option<SampleSizeData>,
    pub sample_to_chunk: Option<SampleToChunkData>,
    pub time_to_sample: Option<TimeToSampleData>,
}

impl<M: MediaType> AtomData for SampleTableData<M> {
    const TYPE: FourCC = FourCC(0x7374626c);

    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
        Ok(Self{
            sample_description: read_one(&mut reader)?.ok_or(Error::MalformedFile("missing sample description"))?,
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
}

#[derive(Clone)]
pub struct SampleDescriptionData<M: MediaType> {
    pub entries: Vec<M::SampleDescriptionDataEntry>,
}

impl<M: MediaType> AtomData for SampleDescriptionData<M> {
    const TYPE: FourCC = FourCC(0x73747364);

    fn read<R: Read + Seek>(mut reader: R) -> Result<Self> {
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
            entries.push(M::SampleDescriptionDataEntry::read(buf.as_slice())?);
        }
        Ok(Self{
            entries: entries,
        })
    }
}
