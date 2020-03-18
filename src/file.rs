use std::io::{copy, Cursor, Read, Seek, SeekFrom, Write};
use std::path::Path;

use super::atom::{AtomReader, AtomSize, AtomWriteExt, FourCC};
use super::{data, data::{AtomData, MovieData, ReadData}};
use super::error::{Error, Result};

pub struct File {
    f: std::fs::File,
    movie_data: Option<MovieData>,
}

impl File {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<File> {
        Ok(File{
            f: std::fs::File::open(path)?,
            movie_data: None,
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
            },
        }
    }

    pub fn trim_frames<W: Write>(&mut self, w: W, start_frame: u64, frame_count: u64) -> Result<()> {
        let movie_data = self.get_movie_data()?;

        let video_track = movie_data.tracks.iter().find(|t| match t.media.information.as_ref() {
            Some(data::MediaInformationData::Video(_)) => true,
            _ => false,
        }).ok_or(Error::Other("no video track"))?;

        let video_minf = match &video_track.media.information {
            Some(data::MediaInformationData::Video(minf)) => minf,
            _ => unreachable!(),
        };

        let time_scale = video_track.media.header.time_scale;
        let sample_table = video_minf.sample_table.as_ref().ok_or(Error::Other("no sample table for video"))?;
        let start_time = sample_table.time_to_sample.as_ref().and_then(|stts| stts.sample_time(start_frame)).ok_or(Error::Other("start frame not found"))?;
        let end_time = sample_table.time_to_sample.as_ref().and_then(|stts| stts.sample_time(start_frame + frame_count)).ok_or(Error::Other("end frame not found"))?;

        self.trim(w, time_scale, start_time, end_time - start_time)
    }

    pub fn trim<W: Write>(&mut self, mut w: W, time_scale: u32, start: u64, duration: u64) -> Result<()> {
        let mut moov: Vec<u8> = Vec::new();
        let mut mdat: Vec<(u64, u32)> = Vec::new();

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
                },
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

                    let time_to_sample = source_time_to_sample.map(|stts| stts.trimmed(start_sample, sample_count));

                    let sample_to_chunk = track.media.information.as_ref().and_then(|minf| match minf {
                        data::MediaInformationData::Sound(minf) => minf.sample_table.as_ref().and_then(|v| v.sample_to_chunk.as_ref()),
                        data::MediaInformationData::Video(minf) => minf.sample_table.as_ref().and_then(|v| v.sample_to_chunk.as_ref()),
                        data::MediaInformationData::Base(minf) => minf.sample_table.as_ref().and_then(|v| v.sample_to_chunk.as_ref()),
                    }.map(|stsc| {
                        stsc.trimmed(start_sample, sample_count)
                    }));

                    let sample_size = track.media.information.as_ref().and_then(|minf| match minf {
                        data::MediaInformationData::Sound(minf) => minf.sample_table.as_ref().and_then(|v| v.sample_size.as_ref()),
                        data::MediaInformationData::Video(minf) => minf.sample_table.as_ref().and_then(|v| v.sample_size.as_ref()),
                        data::MediaInformationData::Base(minf) => minf.sample_table.as_ref().and_then(|v| v.sample_size.as_ref()),
                    }.map(|stsz| {
                        stsz.trimmed(start_sample, sample_count)
                    }));

                    // TODO: interleave data for better performance?
                    let chunk_offset = sample_to_chunk.as_ref().and_then(|stsc| {
                        let mut out = data::ChunkOffset64Data::default();
                        if sample_count == 0 {
                            return Some(out);
                        }

                        let total_chunks = stsc.sample_chunk(sample_count - 1) + 1;
                        out.offsets.resize(total_chunks as usize, 0);

                        let minf = track.media.information.as_ref()?;
                        let mut sample_chunk_info_hint = None;
                        let sample_offset = |n: u64, hint: &mut Option<data::SampleChunkInfo>| -> Option<u64> {
                            match minf {
                                data::MediaInformationData::Sound(minf) => minf.sample_table.as_ref().and_then(|stbl| {
                                    let chunk_info = stbl.sample_chunk_info(n, hint.as_ref())?;
                                    let ret = stbl.sample_offset(n, &chunk_info);
                                    *hint = Some(chunk_info);
                                    ret
                                }),
                                data::MediaInformationData::Video(minf) => minf.sample_table.as_ref().and_then(|stbl| {
                                    let chunk_info = stbl.sample_chunk_info(n, hint.as_ref())?;
                                    let ret = stbl.sample_offset(n, &chunk_info);
                                    *hint = Some(chunk_info);
                                    ret
                                }),
                                data::MediaInformationData::Base(minf) => minf.sample_table.as_ref().and_then(|stbl| {
                                    let chunk_info = stbl.sample_chunk_info(n, hint.as_ref())?;
                                    let ret = stbl.sample_offset(n, &chunk_info);
                                    *hint = Some(chunk_info);
                                    ret
                                }),
                            }
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
                                out.offsets[chunk as usize] = data_offset;

                                let chunk_end_sample = chunk_start_sample + e.samples_per_chunk as u64;

                                let source_start_offset = sample_offset(start_sample + chunk_start_sample, &mut sample_chunk_info_hint)?;
                                let source_end_offset = sample_offset(start_sample + chunk_end_sample, &mut sample_chunk_info_hint)?;

                                let chunk_size = source_end_offset - source_start_offset;
                                mdat.push((source_start_offset, chunk_size as _));
                                data_offset += chunk_size;

                                chunk_start_sample = chunk_end_sample;
                            }
                        }
                        Some(out)
                    });

                    // TODO: for better compatibility with lazy player / decoder implementations, we
                    // should modify the first timecode sample so we don't have to use an edit list
                    // offset. the braw sdk seems to do this, and the braw player seems to totally
                    // ignore edit lists

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

                                trak.write_atom(data::EditData{
                                    edit_list: Some(data::EditListData{
                                        version: 0,
                                        flags: [0; 3],
                                        entries: vec![data::EditListDataEntry{
                                            track_duration: duration as _,
                                            media_time: (start_time as i64 - start_sample_time as i64) as _,
                                            media_rate: 1.0.into(),
                                        }],
                                    }),
                                })?;
                            },
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
                                        },
                                        data::BaseMediaInformationData::TYPE => {
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
                                                            match atom.typ {
                                                                data::SampleDescriptionData::<data::GeneralMediaType>::TYPE => atom.copy(&mut r, &mut stbl)?,
                                                                _ => {},
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
                                                    },
                                                    data::BaseMediaInformationHeaderData::TYPE |
                                                    data::SoundMediaInformationHeaderData::TYPE |
                                                    data::VideoMediaInformationHeaderData::TYPE |
                                                    data::DataInformationData::TYPE |
                                                    data::HandlerReferenceData::TYPE => atom.copy(&mut r, &mut minf)?,
                                                    _ => {},
                                                }
                                            }

                                            mdia.write_atom_header(data::BaseMediaInformationData::TYPE, minf.len())?;
                                            copy(&mut minf.as_slice(), &mut mdia)?;
                                        },
                                        data::HandlerReferenceData::TYPE | data::ExtendedLanguageTagData::TYPE | data::UserDataData::TYPE => atom.copy(&mut r, &mut mdia)?,
                                        _ => {},
                                    }
                                }

                                trak.write_atom_header(data::MediaData::TYPE, mdia.len())?;
                                copy(&mut mdia.as_slice(), &mut trak)?;
                            },
                            data::TrackReferenceData::TYPE | data::UserDataData::TYPE => atom.copy(&mut r, &mut trak)?,
                            _ => {},
                        }
                    }

                    moov.write_atom_header(data::TrackData::TYPE, trak.len())?;
                    copy(&mut trak.as_slice(), &mut moov)?;
                },
                data::MetadataData::TYPE | data::UserDataData::TYPE => atom.copy(r.clone(), &mut moov)?,
                _ => {},
            }
        }

        let mdat_data_size = mdat.iter().fold(0 as u64, |acc, (_, size)| acc + *size as u64);
        w.write_atom_header(FourCC::from_str("mdat"), AtomSize::ExtendedSize(mdat_data_size))?;
        for (offset, size) in mdat.drain(..) {
            self.f.seek(SeekFrom::Start(offset))?;
            copy(&mut (&self.f).take(size as u64), &mut w)?;
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

    #[test]
    fn test_file_prores() {
        let mut f = File::open(Path::new(file!()).parent().unwrap().join("testdata/prores.mov")).unwrap();

        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 3);
    }

    #[test]
    fn test_file_braw() {
        let mut f = File::open(Path::new(file!()).parent().unwrap().join("testdata/braw.braw")).unwrap();
        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 3);

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

            let mut expected_f = File::open(Path::new(file!()).parent().unwrap().join("testdata/braw_trimmed.braw")).unwrap();
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

                if expected.media.handler_reference.as_ref().map(|r| r.component_subtype) == Some(FourCC::from_str("tmcd")) {
                    expected.edit = actual.edit.clone();
                    expected.media.header.duration = actual.media.header.duration.clone();
                }

                match (&mut expected.media.information, &actual.media.information) {
                    (Some(data::MediaInformationData::Video(expected)), Some(data::MediaInformationData::Video(actual))) => {
                        expected.sample_table.as_mut().unwrap().chunk_offset_64 = actual.sample_table.as_ref().unwrap().chunk_offset_64.clone();
                    },
                    (Some(data::MediaInformationData::Sound(expected)), Some(data::MediaInformationData::Sound(actual))) => {
                        expected.sample_table.as_mut().unwrap().chunk_offset_64 = actual.sample_table.as_ref().unwrap().chunk_offset_64.clone();
                    },
                    (Some(data::MediaInformationData::Base(expected)), Some(data::MediaInformationData::Base(actual))) => {
                        expected.sample_table.as_mut().unwrap().time_to_sample = actual.sample_table.as_ref().unwrap().time_to_sample.clone();
                        expected.sample_table.as_mut().unwrap().chunk_offset_64 = actual.sample_table.as_ref().unwrap().chunk_offset_64.clone();
                    },
                    _ => panic!("mismatched media information"),
                }

                assert_eq!(expected, actual);
            }
        }
    }

    #[test]
    fn test_file_h264_mp4() {
        let mut f = File::open(Path::new(file!()).parent().unwrap().join("testdata/h264.mp4")).unwrap();

        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 2);
    }
}
