use std::io::{copy, Cursor, Read, Seek, SeekFrom, Write};
use std::path::Path;

use super::atom::{AtomReader, AtomSize, AtomWriteExt, FourCC};
use super::error::{Error, Result};
use super::{
    data,
    data::{AtomData, MovieData, ReadData},
};

use crate::moof::MovieFragment;
use byteorder::{BigEndian, ReadBytesExt, WriteBytesExt};

pub struct File {
    f: std::fs::File,
    movie_data: Option<MovieData>,
    movie_fragments: Option<Vec<(u64, MovieFragment)>>,
}

enum Data {
    SourceFile(u64, usize),
    Vec(Vec<u8>),
}

impl Data {
    fn len(&self) -> usize {
        match self {
            Self::SourceFile(_, size) => *size,
            Self::Vec(v) => v.len(),
        }
    }
}

impl File {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<File> {
        Ok(File {
            f: std::fs::File::open(path)?,
            movie_data: None,
            movie_fragments: None,
        })
    }

    fn read_moov_data(&mut self) -> Result<Vec<u8>> {
        self.f.seek(SeekFrom::Start(0))?;
        let atom = match AtomReader::new(&mut self.f).find(|a| match a {
            Ok(a) => a.typ == MovieData::TYPE,
            Err(_) => true,
        }) {
            Some(Ok(a)) => a,
            Some(Err(err)) => return Err(err.into()),
            None => return Err(Error::MalformedFile("missing movie")),
        };
        // minimize reads by pre-loading the entire atom
        let mut buf = Vec::new();
        atom.data(&mut self.f).read_to_end(&mut buf)?;
        Ok(buf)
    }

    pub fn get_movie_data(&mut self) -> Result<MovieData> {
        match &self.movie_data {
            Some(v) => Ok(v.clone()),
            None => {
                let buf = self.read_moov_data()?;
                let data = MovieData::read(Cursor::new(buf.as_slice()))?;
                self.movie_data = Some(data.clone());
                Ok(data)
            }
        }
    }

    fn get_moof_data(&mut self) -> Result<Vec<(u64, MovieFragment)>> {
        let mut fragments = vec![];
        loop {
            let start_position = self.f.stream_position()?;

            let atom = match AtomReader::new(&mut self.f).find(|a| match a {
                Ok(a) => a.typ == MovieFragment::TYPE,
                Err(_) => true,
            }) {
                Some(Ok(a)) => a,
                Some(Err(err)) => return Err(err.into()),
                None => return Ok(fragments),
            };
            let mut buf = Vec::new();
            atom.data(&mut self.f).read_to_end(&mut buf)?;

            fragments.push((start_position, MovieFragment::read(Cursor::new(buf.as_slice()))?));
        }
    }

    pub fn get_movie_fragments(&mut self) -> Result<Vec<(u64, MovieFragment)>> {
        if self.movie_data.is_none() {
            self.get_movie_data()?;
        }
        match &self.movie_fragments {
            Some(v) => Ok(v.clone()),
            None => {
                let movie_fragments = self.get_moof_data()?;
                self.movie_fragments = Some(movie_fragments.clone());
                Ok(movie_fragments)
            }
        }
    }

    pub fn trim_frames<W: Write>(&mut self, w: W, start_frame: u64, frame_count: u64) -> Result<()> {
        let movie_data = self.get_movie_data()?;

        let video_track = movie_data
            .tracks
            .iter()
            .find(|t| matches!(t.media.information.as_ref(), Some(data::MediaInformationData::Video(_))))
            .ok_or(Error::Other("no video track"))?;

        let video_minf = match &video_track.media.information {
            Some(data::MediaInformationData::Video(minf)) => minf,
            _ => unreachable!(),
        };

        let time_scale = video_track.media.header.time_scale;
        let sample_table = video_minf.sample_table.as_ref().ok_or(Error::Other("no sample table for video"))?;
        let start_time = sample_table
            .time_to_sample
            .as_ref()
            .and_then(|stts| stts.sample_time(start_frame))
            .ok_or(Error::Other("start frame not found"))?;
        let end_time = sample_table
            .time_to_sample
            .as_ref()
            .and_then(|stts| stts.sample_time(start_frame + frame_count))
            .ok_or(Error::Other("end frame not found"))?;

        self.trim(w, time_scale, start_time, end_time - start_time)
    }

    fn trim_sample_table<M: Clone + data::MediaType>(
        source: &data::SampleTableData<M>,
        start_sample: u64,
        sample_count: u64,
        data_offset: &mut u64,
    ) -> (data::SampleTableData<M>, Vec<Data>) {
        let mut dest = data::SampleTableData::default();
        let mut mdat: Vec<Data> = Vec::new();

        dest.sample_description = source.sample_description.clone();
        dest.time_to_sample = source.time_to_sample.as_ref().map(|stts| stts.trimmed(start_sample, sample_count));
        dest.sample_to_chunk = source.sample_to_chunk.as_ref().map(|stsc| stsc.trimmed(start_sample, sample_count));
        dest.sample_size = source.sample_size.as_ref().map(|stsz| stsz.trimmed(start_sample, sample_count));

        // TODO: interleave data for better performance?
        dest.chunk_offset_64 = dest.sample_to_chunk.as_ref().and_then(|stsc| {
            let mut out = data::ChunkOffset64Data::default();
            if sample_count == 0 {
                return Some(out);
            }

            let total_chunks = stsc.sample_chunk(sample_count - 1) + 1;
            out.offsets.resize(total_chunks as usize, 0);

            let mut sample_chunk_info_hint = None;
            let source_sample_offset = |n: u64, hint: &mut Option<data::SampleChunkInfo>| -> Option<u64> {
                let chunk_info = source.sample_chunk_info(n, hint.as_ref())?;
                let ret = source.sample_offset(n, &chunk_info);
                *hint = Some(chunk_info);
                ret
            };
            let source_sample_size = |n: u64, hint: &mut Option<data::SampleChunkInfo>| -> Option<u32> {
                let chunk_info = source.sample_chunk_info(n, hint.as_ref())?;
                let ret = source.sample_size(n, &chunk_info);
                *hint = Some(chunk_info);
                ret
            };

            let mut chunk_start_sample = 0;
            for i in 0..stsc.entries.len() {
                let e = &stsc.entries[i];
                let end_chunk = if i + 1 < stsc.entries.len() {
                    stsc.entries[i + 1].first_chunk - 1
                } else {
                    total_chunks
                };
                for chunk in (e.first_chunk - 1)..end_chunk {
                    out.offsets[chunk as usize] = *data_offset;

                    let chunk_end_sample = chunk_start_sample + e.samples_per_chunk as u64;

                    let source_start_offset = source_sample_offset(start_sample + chunk_start_sample, &mut sample_chunk_info_hint)?;
                    let last_sample_size = source_sample_size(start_sample + chunk_end_sample - 1, &mut sample_chunk_info_hint)?;
                    let source_end_offset = source_sample_offset(start_sample + chunk_end_sample - 1, &mut sample_chunk_info_hint)? + last_sample_size as u64;

                    let chunk_size = source_end_offset - source_start_offset;
                    mdat.push(Data::SourceFile(source_start_offset, chunk_size as _));
                    *data_offset += chunk_size;

                    chunk_start_sample = chunk_end_sample;
                }
            }
            Some(out)
        });

        (dest, mdat)
    }

    #[allow(clippy::cognitive_complexity)]
    pub fn trim<W: Write>(&mut self, mut w: W, time_scale: u32, start: u64, duration: u64) -> Result<()> {
        let mut moov: Vec<u8> = Vec::new();
        let mut mdat: Vec<Data> = Vec::new();

        let mut data_offset = 16;
        let start_time_secs = (start as f64) / (time_scale as f64);
        let end_time_secs = ((start + duration) as f64) / (time_scale as f64);

        let moov_data = self.read_moov_data()?;
        let mut r = Cursor::new(moov_data.as_slice());
        for atom in AtomReader::new(&mut r).collect::<Vec<_>>().drain(..) {
            let atom = atom?;
            match atom.typ {
                data::MovieHeaderData::TYPE => {
                    let mut data = data::MovieHeaderData::read(atom.data(r.clone()))?;
                    data.version = 0;
                    data.time_scale = time_scale as _;
                    data.duration = duration as _;
                    data.preview_time = 0;
                    data.preview_duration = 0;
                    data.poster_time = 0;
                    data.selection_time = 0;
                    data.selection_duration = 0;
                    data.current_time = 0;
                    moov.write_atom(data)?;
                }
                data::TrackData::TYPE => {
                    let track = data::TrackData::read(atom.data(r.clone()))?;

                    let mut start_time = (start_time_secs * track.media.header.time_scale as f64).round() as u64;
                    let mut end_time = (end_time_secs * track.media.header.time_scale as f64).round() as u64;

                    if let Some(edit) = &track.edit {
                        if let Some(list) = &edit.edit_list {
                            if list.entries.len() > 1 {
                                return Err(Error::Other("complex edit lists are not supported"));
                            } else if !list.entries.is_empty() {
                                let entry = &list.entries[0];
                                if entry.media_rate != 1.0.into() {
                                    return Err(Error::Other("edit list media rates are not supported"));
                                }
                                let offset = entry.media_time;
                                if offset > 0 {
                                    start_time += offset as u64;
                                    end_time += offset as u64;
                                }
                            }
                        }
                    }

                    let source_time_to_sample = track.media.information.as_ref().and_then(|minf| match minf {
                        data::MediaInformationData::Sound(minf) => minf.sample_table.as_ref().and_then(|v| v.time_to_sample.as_ref()),
                        data::MediaInformationData::Timecode(minf) => minf.sample_table.as_ref().and_then(|v| v.time_to_sample.as_ref()),
                        data::MediaInformationData::Video(minf) => minf.sample_table.as_ref().and_then(|v| v.time_to_sample.as_ref()),
                        data::MediaInformationData::Base(minf) => minf.sample_table.as_ref().and_then(|v| v.time_to_sample.as_ref()),
                    });

                    let mut start_sample = None;
                    let mut end_sample = None;
                    let mut start_sample_time = 0;
                    if let Some(stts) = source_time_to_sample {
                        let mut t = 0;
                        let mut n = 0;
                        for entry in stts.entries.iter() {
                            if entry.sample_duration > 0 {
                                let next_entry_time: u64 = t + (entry.sample_duration as u64) * (entry.sample_count as u64);
                                if next_entry_time > start_time && start_sample.is_none() {
                                    let relative_start_sample = (start_time - t) / (entry.sample_duration as u64);
                                    start_sample = Some(n + relative_start_sample);
                                    start_sample_time = t + relative_start_sample * entry.sample_duration as u64;
                                }
                                if next_entry_time >= end_time && end_sample.is_none() {
                                    end_sample = Some(n + (end_time - t + entry.sample_duration as u64 - 1) / (entry.sample_duration as u64));
                                }
                                t = next_entry_time;
                            }
                            n += entry.sample_count as u64;
                        }
                    }
                    let start_sample = start_sample.unwrap_or(0);
                    let end_sample = end_sample.unwrap_or(0);
                    let sample_count = end_sample - start_sample;

                    let (time_to_sample, sample_to_chunk, sample_size, chunk_offset, mut mdat_additions) = track
                        .media
                        .information
                        .as_ref()
                        .and_then(|minf| -> Option<Result<_>> {
                            match minf {
                                data::MediaInformationData::Sound(minf) => minf.sample_table.as_ref().map(|v| {
                                    let (table, mdat) = Self::trim_sample_table(v, start_sample, sample_count, &mut data_offset);
                                    Ok((table.time_to_sample, table.sample_to_chunk, table.sample_size, table.chunk_offset_64, mdat))
                                }),
                                data::MediaInformationData::Video(minf) => minf.sample_table.as_ref().map(|v| {
                                    let (table, mdat) = Self::trim_sample_table(v, start_sample, sample_count, &mut data_offset);
                                    Ok((table.time_to_sample, table.sample_to_chunk, table.sample_size, table.chunk_offset_64, mdat))
                                }),
                                data::MediaInformationData::Timecode(minf) => minf.sample_table.as_ref().map(|v| {
                                    let (mut table, mut mdat) = Self::trim_sample_table(v, start_sample, sample_count, &mut data_offset);

                                    if sample_count > 0 && !mdat.is_empty() && start_sample_time < start_time {
                                        // modify the first timecode sample so we don't have to use an edit
                                        // list offset for better compatibility
                                        let offset = (start_time - start_sample_time) as u32;

                                        // reduce the duration of the first sample so any samples that come after it are timed correctly
                                        if let Some(time_to_sample) = table.time_to_sample.as_mut() {
                                            if !time_to_sample.entries.is_empty() {
                                                let first = &mut time_to_sample.entries[0];
                                                let new_duration = first.sample_duration - offset;
                                                if first.sample_count == 1 {
                                                    first.sample_duration = new_duration;
                                                } else {
                                                    first.sample_count -= 1;
                                                    let mut new_entries = vec![data::TimeToSampleDataEntry {
                                                        sample_count: 1,
                                                        sample_duration: new_duration,
                                                    }];
                                                    new_entries.append(&mut time_to_sample.entries);
                                                    time_to_sample.entries = new_entries;
                                                }
                                            }
                                        }

                                        let mut data = match &mdat[0] {
                                            Data::SourceFile(source_offset, source_size) => {
                                                self.f.seek(SeekFrom::Start(*source_offset))?;
                                                let mut buf = vec![0; *source_size];
                                                self.f.read_exact(&mut buf)?;
                                                buf
                                            }
                                            Data::Vec(buf) => buf.clone(),
                                        };

                                        let chunk_info = table
                                            .sample_chunk_info(0, None)
                                            .ok_or(Error::Other("Unable to find first timecode sample chunk."))?;
                                        let description = table
                                            .sample_description(chunk_info.sample_description)
                                            .ok_or(Error::Other("No sample description for timecode sample."))?;
                                        let sample_data = data.as_slice().read_u32::<BigEndian>()?;
                                        let offset_secs = (start_time as f64 - start_sample_time as f64) / track.media.header.time_scale as f64;
                                        let offset_frames = (offset_secs * description.fps()).round() as i64;
                                        let new_timecode = description.add_frames_to_sample(description.parse_sample_data(sample_data), offset_frames);
                                        (&mut data.as_mut_slice()).write_u32::<BigEndian>(new_timecode.data())?;

                                        mdat[0] = Data::Vec(data);

                                        start_sample_time = start_time;
                                    }

                                    Ok((table.time_to_sample, table.sample_to_chunk, table.sample_size, table.chunk_offset_64, mdat))
                                }),
                                data::MediaInformationData::Base(minf) => minf.sample_table.as_ref().map(|v| {
                                    let (table, mdat) = Self::trim_sample_table(v, start_sample, sample_count, &mut data_offset);
                                    Ok((table.time_to_sample, table.sample_to_chunk, table.sample_size, table.chunk_offset_64, mdat))
                                }),
                            }
                        })
                        .unwrap_or_else(|| Ok((None, None, None, None, vec![])))?;
                    mdat.append(&mut mdat_additions);

                    let mut trak: Vec<u8> = Vec::new();

                    let mut r = atom.data(r.clone());
                    for atom in AtomReader::new(&mut r).collect::<Vec<_>>().drain(..) {
                        let atom = atom?;
                        match atom.typ {
                            data::TrackHeaderData::TYPE => {
                                let mut data = data::TrackHeaderData::read(atom.data(&mut r))?;
                                data.version = 0;
                                data.duration = duration as _;
                                trak.write_atom(data)?;

                                trak.write_atom(data::EditData {
                                    edit_list: Some(data::EditListData {
                                        version: 0,
                                        flags: [0; 3],
                                        entries: vec![data::EditListDataEntry {
                                            track_duration: duration as _,
                                            media_time: (start_time as i64 - start_sample_time as i64) as _,
                                            media_rate: 1.0.into(),
                                        }],
                                    }),
                                })?;
                            }
                            data::MediaData::TYPE => {
                                let mut mdia: Vec<u8> = Vec::new();

                                let mut r = atom.data(&mut r);
                                for atom in AtomReader::new(&mut r).collect::<Vec<_>>().drain(..) {
                                    let atom = atom?;
                                    match atom.typ {
                                        data::MediaHeaderData::TYPE => {
                                            let mut data = data::MediaHeaderData::read(atom.data(&mut r))?;
                                            data.version = 0;
                                            data.duration = time_to_sample.as_ref().map(|stts| stts.duration()).unwrap_or(0) as _;
                                            mdia.write_atom(data)?;
                                        }
                                        data::BaseMediaInformationData::<data::GeneralMediaType>::TYPE => {
                                            let mut minf: Vec<u8> = Vec::new();

                                            let mut r = atom.data(&mut r);
                                            for atom in AtomReader::new(&mut r).collect::<Vec<_>>().drain(..) {
                                                let atom = atom?;
                                                match atom.typ {
                                                    data::SampleTableData::<data::GeneralMediaType>::TYPE => {
                                                        let mut stbl: Vec<u8> = Vec::new();

                                                        let mut r = atom.data(&mut r);
                                                        for atom in AtomReader::new(&mut r).collect::<Vec<_>>().drain(..) {
                                                            let atom = atom?;
                                                            if let data::SampleDescriptionData::<data::GeneralMediaType>::TYPE = atom.typ {
                                                                atom.copy(&mut r, &mut stbl)?
                                                            }
                                                        }

                                                        if let Some(atom) = time_to_sample.as_ref() {
                                                            stbl.write_atom(atom.clone())?;
                                                        }

                                                        if let Some(atom) = sample_to_chunk.as_ref() {
                                                            stbl.write_atom(atom.clone())?;
                                                        }

                                                        if let Some(atom) = sample_size.as_ref() {
                                                            stbl.write_atom(atom.clone())?;
                                                        }

                                                        if let Some(atom) = chunk_offset.as_ref() {
                                                            stbl.write_atom(atom.clone())?;
                                                        }

                                                        minf.write_atom_header(data::SampleTableData::<data::GeneralMediaType>::TYPE, stbl.len())?;
                                                        copy(&mut stbl.as_slice(), &mut minf)?;
                                                    }
                                                    data::BaseMediaInformationHeaderData::TYPE
                                                    | data::SoundMediaInformationHeaderData::TYPE
                                                    | data::VideoMediaInformationHeaderData::TYPE
                                                    | data::DataInformationData::TYPE
                                                    | data::HandlerReferenceData::TYPE => atom.copy(&mut r, &mut minf)?,
                                                    _ => {}
                                                }
                                            }

                                            mdia.write_atom_header(data::BaseMediaInformationData::<data::GeneralMediaType>::TYPE, minf.len())?;
                                            copy(&mut minf.as_slice(), &mut mdia)?;
                                        }
                                        data::HandlerReferenceData::TYPE | data::ExtendedLanguageTagData::TYPE | data::UserDataData::TYPE => {
                                            atom.copy(&mut r, &mut mdia)?
                                        }
                                        _ => {}
                                    }
                                }

                                trak.write_atom_header(data::MediaData::TYPE, mdia.len())?;
                                copy(&mut mdia.as_slice(), &mut trak)?;
                            }
                            data::TrackReferenceData::TYPE | data::UserDataData::TYPE => atom.copy(&mut r, &mut trak)?,
                            _ => {}
                        }
                    }

                    moov.write_atom_header(data::TrackData::TYPE, trak.len())?;
                    copy(&mut trak.as_slice(), &mut moov)?;
                }
                data::MetadataData::TYPE | data::UserDataData::TYPE => atom.copy(r.clone(), &mut moov)?,
                _ => {}
            }
        }

        let mdat_data_size = mdat.iter().fold(0_u64, |acc, data| acc + data.len() as u64);
        w.write_atom_header(FourCC::from_str("mdat"), AtomSize::ExtendedSize(mdat_data_size))?;
        for mut data in mdat.drain(..) {
            match &mut data {
                Data::SourceFile(offset, size) => {
                    self.f.seek(SeekFrom::Start(*offset))?;
                    copy(&mut (&self.f).take(*size as u64), &mut w)?;
                }
                Data::Vec(buf) => {
                    copy(&mut buf.as_slice(), &mut w)?;
                }
            }
        }

        w.write_atom_header(MovieData::TYPE, moov.len())?;
        copy(&mut moov.as_slice(), &mut w)?;

        Ok(())
    }
}

impl Read for File {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.f.read(buf)
    }
}

impl<'a> Read for &'a File {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        (&self.f).read(buf)
    }
}

impl Seek for File {
    fn seek(&mut self, pos: std::io::SeekFrom) -> std::io::Result<u64> {
        self.f.seek(pos)
    }
}

impl<'a> Seek for &'a File {
    fn seek(&mut self, pos: std::io::SeekFrom) -> std::io::Result<u64> {
        (&self.f).seek(pos)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::moof::{FragmentHeader, TrackFragmentHeader, TrackFragmentRunSampleData};

    #[test]
    fn test_file_prores() {
        let mut f = File::open("src/testdata/prores.mov").unwrap();

        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 3);

        for track in movie_data.tracks.iter() {
            if let data::MediaInformationData::Sound(minf) = track.media.information.as_ref().unwrap() {
                let desc = &minf.sample_table.as_ref().unwrap().sample_description.as_ref().unwrap().entries[0];
                assert_eq!(
                    &data::SoundSampleDescriptionDataEntry {
                        data_format: 1_768_829_492,
                        reserved: [0, 0, 0, 0, 0, 0],
                        data_reference_index: 1,
                        version: data::SoundSampleDescriptionDataEntryVersion::V1(data::SoundSampleDescriptionDataEntryV1 {
                            revision_level: 0,
                            vendor: 0,
                            number_of_channels: 2,
                            sample_size: 16,
                            compression_id: 0,
                            packet_size: 0,
                            sample_rate: 48_000.0.into(),
                            samples_per_packet: 1,
                            bytes_per_packet: 3,
                            bytes_per_frame: 6,
                            bytes_per_sample: 2,
                            extensions: data::SoundSampleDescriptionDataEntryExtensions {
                                elementary_stream_descriptor: None,
                            },
                        }),
                    },
                    desc
                );
            }
        }
    }

    #[test]
    fn test_file_braw() {
        let mut f = File::open("src/testdata/braw.braw").unwrap();
        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 3);

        {
            let metadata = movie_data.metadata.as_ref().unwrap().metadata();
            for (k, v) in [
                ("manufacturer", data::MetadataValue::String("Blackmagic Design".to_string())),
                ("camera_id", data::MetadataValue::String("7860b0bc-64aa-416b-919a-fdef19fb483c".to_string())),
                ("camera_type", data::MetadataValue::String("Blackmagic URSA Mini Pro 4.6K".to_string())),
                ("firmware_version", data::MetadataValue::String("6.0".to_string())),
                ("braw_compression_ratio", data::MetadataValue::String("12:1".to_string())),
                ("crop_origin", data::MetadataValue::Dimensions { width: 16.0, height: 16.0 }),
                ("crop_size", data::MetadataValue::Dimensions { width: 4608.0, height: 2592.0 }),
                ("clip_number", data::MetadataValue::String("A057_08251201_C159".to_string())),
                ("reel_name", data::MetadataValue::String("57".to_string())),
                ("scene", data::MetadataValue::String("1".to_string())),
                ("take", data::MetadataValue::String("99".to_string())),
                ("good_take", data::MetadataValue::String("false".to_string())),
                ("environment", data::MetadataValue::String("interior".to_string())),
                ("day_night", data::MetadataValue::String("day".to_string())),
                ("lens_type", data::MetadataValue::String("Sigma or Tamron 24-70mm f/2.8".to_string())),
                ("camera_number", data::MetadataValue::String("A".to_string())),
                ("aspect_ratio", data::MetadataValue::String("2.40:1".to_string())),
                ("tone_curve_contrast", data::MetadataValue::F32(1.410_178)),
                ("tone_curve_saturation", data::MetadataValue::F32(1.0)),
                ("tone_curve_midpoint", data::MetadataValue::F32(0.409_008)),
                ("tone_curve_highlights", data::MetadataValue::F32(0.221_778)),
                ("tone_curve_shadows", data::MetadataValue::F32(1.633_367)),
                ("tone_curve_video_black_level", data::MetadataValue::U16(0)),
                ("post_3dlut_mode", data::MetadataValue::String("Disabled".to_string())),
                ("viewing_gamma", data::MetadataValue::String("Blackmagic Design Extended Video".to_string())),
                ("viewing_gamut", data::MetadataValue::String("Blackmagic Design".to_string())),
                ("viewing_bmdgen", data::MetadataValue::U16(4)),
                ("date_recorded", data::MetadataValue::String("2018:08:25".to_string())),
            ]
            .iter()
            {
                let values = metadata.get(&(*k).to_string());
                assert!(values.is_some(), "{}", k);
                let values = values.unwrap();
                assert_eq!(values.len(), 1, "{}", k);
                assert_eq!(values[0].value, *v, "{}", k);
            }
        }

        {
            let dir = tempfile::TempDir::new().unwrap();
            let path = dir.path().join("tmp.mov");
            {
                let mut f_out = std::fs::File::create(&path).unwrap();
                f.trim_frames(&mut f_out, 1, 2).unwrap();
            }
            let mut f = File::open(path).unwrap();
            let mut movie_data = f.get_movie_data().unwrap();
            assert_eq!(movie_data.tracks.len(), 3);

            let mut expected_f = File::open("src/testdata/braw_trimmed.braw").unwrap();
            let mut expected_movie_data = expected_f.get_movie_data().unwrap();
            assert_eq!(expected_movie_data.tracks.len(), 3);

            expected_movie_data.header.creation_time = movie_data.header.creation_time;
            expected_movie_data.header.modification_time = movie_data.header.modification_time;
            assert_eq!(expected_movie_data.header, movie_data.header);

            for (mut expected, actual) in expected_movie_data.tracks.drain(..).zip(movie_data.tracks.drain(..)) {
                expected.header.creation_time = actual.header.creation_time;
                expected.header.modification_time = actual.header.modification_time;

                expected.media.header.creation_time = actual.media.header.creation_time;
                expected.media.header.modification_time = actual.media.header.modification_time;

                match (&mut expected.media.information, &actual.media.information) {
                    (Some(data::MediaInformationData::Video(expected)), Some(data::MediaInformationData::Video(actual))) => {
                        expected.sample_table.as_mut().unwrap().chunk_offset_64 = actual.sample_table.as_ref().unwrap().chunk_offset_64.clone();
                        expected.sample_table.as_mut().unwrap().sample_description = actual.sample_table.as_ref().unwrap().sample_description.clone();
                    }
                    (Some(data::MediaInformationData::Sound(expected)), Some(data::MediaInformationData::Sound(actual))) => {
                        expected.sample_table.as_mut().unwrap().chunk_offset_64 = actual.sample_table.as_ref().unwrap().chunk_offset_64.clone();
                    }
                    (Some(data::MediaInformationData::Timecode(expected_minf)), Some(data::MediaInformationData::Timecode(actual_minf))) => {
                        let expected_stbl = expected_minf.sample_table.as_ref().unwrap();
                        let expected_sample_offset = expected_stbl.sample_offset(0, &expected_stbl.sample_chunk_info(0, None).unwrap()).unwrap();
                        let actual_stbl = actual_minf.sample_table.as_ref().unwrap();
                        let actual_sample_offset = actual_stbl.sample_offset(0, &actual_stbl.sample_chunk_info(0, None).unwrap()).unwrap();
                        expected_f.seek(SeekFrom::Start(expected_sample_offset)).unwrap();
                        f.seek(SeekFrom::Start(actual_sample_offset)).unwrap();
                        assert_eq!(expected_f.read_u32::<BigEndian>().unwrap(), f.read_u32::<BigEndian>().unwrap());

                        expected.media.header.duration = actual.media.header.duration;
                        expected_minf.sample_table.as_mut().unwrap().time_to_sample = actual_minf.sample_table.as_ref().unwrap().time_to_sample.clone();
                        expected_minf.sample_table.as_mut().unwrap().chunk_offset_64 = actual_minf.sample_table.as_ref().unwrap().chunk_offset_64.clone();
                    }
                    (Some(data::MediaInformationData::Base(expected)), Some(data::MediaInformationData::Base(actual))) => {
                        expected.sample_table.as_mut().unwrap().chunk_offset_64 = actual.sample_table.as_ref().unwrap().chunk_offset_64.clone();
                    }
                    _ => panic!("mismatched media information"),
                }

                assert_eq!(expected, actual);
            }
        }
    }

    #[test]
    fn test_file_braw_trim_full() {
        let mut f = File::open("src/testdata/braw.braw").unwrap();

        let dir = tempfile::TempDir::new().unwrap();
        let path = dir.path().join("tmp.mov");
        let mut f_out = std::fs::File::create(path).unwrap();
        f.trim_frames(&mut f_out, 0, 4).unwrap();
    }

    #[test]
    fn test_file_h264_mp4() {
        let mut f = File::open("src/testdata/h264.mp4").unwrap();

        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 2);

        for track in movie_data.tracks.iter() {
            if let data::MediaInformationData::Sound(minf) = track.media.information.as_ref().unwrap() {
                let desc = &minf.sample_table.as_ref().unwrap().sample_description.as_ref().unwrap().entries[0];
                assert_eq!(
                    &data::SoundSampleDescriptionDataEntry {
                        data_format: 1_836_069_985,
                        reserved: [0, 0, 0, 0, 0, 0],
                        data_reference_index: 1,
                        version: data::SoundSampleDescriptionDataEntryVersion::V0(data::SoundSampleDescriptionDataEntryV0 {
                            revision_level: 0,
                            vendor: 0,
                            number_of_channels: 2,
                            sample_size: 16,
                            compression_id: 0,
                            packet_size: 0,
                            sample_rate: 48000.0.into(),
                            extensions: data::SoundSampleDescriptionDataEntryExtensions {
                                elementary_stream_descriptor: Some(data::ElementaryStreamDescriptorData {
                                    version: 0,
                                    descriptor: vec![
                                        0x03, 0x80, 0x80, 0x80, 0x22, 0x00, 0x02, 0x00, 0x04, 0x80, 0x80, 0x80, 0x14, 0x40, 0x15, 0x00, 0x00, 0x00, 0x00, 0x02,
                                        0xe3, 0xbf, 0x00, 0x02, 0xe3, 0xbf, 0x05, 0x80, 0x80, 0x80, 0x02, 0x11, 0x90, 0x06, 0x80, 0x80, 0x80, 0x01, 0x02,
                                    ],
                                }),
                            },
                        }),
                    },
                    desc
                );
            }
        }
    }

    #[test]
    fn test_file_h265_mp4() {
        let mut f = File::open("src/testdata/h265.mp4").unwrap();
        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 2);
    }

    #[test]
    fn test_file_empty_mov() {
        let mut f = File::open("src/testdata/empty.mov").unwrap();
        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 3);
    }

    #[test]
    fn test_file_fragmented_mp4() {
        let mut f = File::open("src/testdata/fragmented.mp4").unwrap();
        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 1);
        let fragments = f.get_movie_fragments().unwrap();
        assert_eq!(fragments.len(), 2);

        let fragment = &fragments[0];
        assert_eq!(fragment.0, 834);
        assert_eq!(
            fragment.1.fragment_header,
            FragmentHeader {
                version: 0,
                flags: 0,
                sequence_number: 1,
            }
        );
        assert_eq!(fragment.1.track_fragments.len(), 1);

        let track_fragment = &fragment.1.track_fragments[0];
        assert_eq!(
            track_fragment.track_fragment_header,
            TrackFragmentHeader {
                version: 0,
                flags: 57,
                track_id: 1,
                base_data_offset: Some(834),
                sample_description_index: None,
                default_sample_duration: Some(3003),
                default_sample_size: Some(1288964),
                default_sample_flags: Some(16842752),
                duration_is_empty: false,
            }
        );

        assert_eq!(track_fragment.track_fragment_runs.len(), 1);

        let track_fragment_run = &track_fragment.track_fragment_runs[0];
        assert_eq!(track_fragment_run.sample_count, 64);
        assert_eq!(track_fragment_run.data_offset, Some(376));
        assert_eq!(track_fragment_run.first_sample_flags, Some(33554432));
        assert_eq!(track_fragment_run.sample_data.len(), 64);
        assert_eq!(
            track_fragment_run.sample_data[0],
            TrackFragmentRunSampleData {
                sample_duration: None,
                sample_size: Some(1288964,),
                sample_flags: None,
                sample_composition_time_offset: None,
            }
        );
    }
}
