use std::io::{Cursor, Read, Seek};
use std::path::Path;

use super::atom::{AtomReader};
use super::data::{AtomData, MovieData};
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

    pub fn get_movie_data(&mut self) -> Result<MovieData> {
        match &self.movie_data {
            Some(v) => Ok(v.clone()),
            None => {
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
                let data = MovieData::read(Cursor::new(buf.as_slice()))?;
                self.movie_data = Some(data.clone());
                Ok(data)
            },
        }
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
    fn test_file() {
        let mut f = File::open(Path::new(file!()).parent().unwrap().join("testdata/prores.mov")).unwrap();
        let movie_data = f.get_movie_data().unwrap();
        assert_eq!(movie_data.tracks.len(), 3);
    }
}
