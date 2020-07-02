use std::{
    io,
    io::{copy, Read, Seek, SeekFrom, Write},
};

use byteorder::{BigEndian, ByteOrder, ReadBytesExt, WriteBytesExt};

use super::data::{AtomData, WriteData};
use super::error::Result;

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Eq)]
pub struct FourCC(pub u32);

impl FourCC {
    pub fn from_str(s: &str) -> FourCC {
        FourCC(BigEndian::read_u32(s.as_ref()))
    }

    pub fn to_string(self) -> Option<String> {
        let mut buf = vec![];
        buf.write_u32::<BigEndian>(self.0).unwrap();
        String::from_utf8(buf).ok()
    }
}

#[derive(Clone, Debug)]
pub enum AtomSize {
    Size(u32),
    ExtendedSize(u64),
}

impl AtomSize {
    pub fn as_usize(&self) -> usize {
        match self {
            AtomSize::Size(n) => *n as _,
            AtomSize::ExtendedSize(n) => *n as _,
        }
    }
}

impl From<AtomSize> for usize {
    fn from(s: AtomSize) -> usize {
        s.as_usize()
    }
}

impl From<usize> for AtomSize {
    fn from(s: usize) -> AtomSize {
        if s <= 0xffffffff {
            AtomSize::Size(s as _)
        } else {
            AtomSize::ExtendedSize(s as _)
        }
    }
}

pub struct SectionReader<R: Read + Seek> {
    reader: R,
    begin: u64,
    end: u64,
    position: u64,
}

impl<R: Read + Seek> SectionReader<R> {
    pub fn new(reader: R, offset: usize, len: usize) -> Self {
        Self {
            reader: reader,
            begin: offset as _,
            end: (offset + len) as _,
            position: offset as _,
        }
    }

    pub fn remaining(&self) -> usize {
        (self.end - self.position) as usize
    }
}

impl<R: Read + Seek> Read for SectionReader<R> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let reader = &mut self.reader;
        reader.seek(SeekFrom::Start(self.position))?;
        let n = reader.take(self.end - self.position).read(buf)?;
        self.position += n as u64;
        Ok(n)
    }
}

impl<R: Read + Seek> Seek for SectionReader<R> {
    fn seek(&mut self, pos: SeekFrom) -> io::Result<u64> {
        self.position = match pos {
            SeekFrom::Start(n) => self.begin + n,
            SeekFrom::End(n) => ((self.end as i64) + n) as u64,
            SeekFrom::Current(n) => ((self.position as i64) + n) as u64,
        };
        Ok(self.position - self.begin)
    }
}

#[derive(Clone, Debug)]
pub struct Atom {
    pub offset: usize,
    pub typ: FourCC,
    pub size: AtomSize,
}

impl Atom {
    pub fn data<R: Read + Seek>(&self, reader: R) -> SectionReader<R> {
        let header_size = match self.size {
            AtomSize::Size(_) => 8,
            AtomSize::ExtendedSize(_) => 16,
        };
        SectionReader::new(reader, self.offset + header_size, self.size.as_usize() - header_size)
    }

    pub fn copy<R: Read + Seek, W: Write>(&self, mut reader: R, mut writer: W) -> io::Result<()> {
        reader.seek(SeekFrom::Start(self.offset as u64))?;
        copy(&mut reader.take(self.size.as_usize() as _), &mut writer).map(|_| ())
    }
}

pub struct AtomReader<R: Read + Seek> {
    offset: Option<usize>,
    reader: R,
}

impl<R: Read + Seek> AtomReader<R> {
    /// Creates a new atom reader with the given reader. Atoms will be read beginning at the
    /// reader's current position.
    pub fn new(reader: R) -> Self {
        Self { offset: None, reader: reader }
    }
}

impl<R: Read + Seek> Iterator for AtomReader<R> {
    type Item = io::Result<Atom>;

    fn next(&mut self) -> Option<Self::Item> {
        let offset = match self.reader.seek(match self.offset {
            Some(offset) => SeekFrom::Start(offset as u64),
            None => SeekFrom::Current(0),
        }) {
            Ok(o) => o as _,
            Err(e) => return Some(Err(e)),
        };
        let mut size = match self.reader.read_u32::<BigEndian>() {
            Ok(n) => AtomSize::Size(n),
            Err(e) => {
                return match e.kind() {
                    io::ErrorKind::UnexpectedEof => None,
                    _ => Some(Err(e)),
                }
            }
        };
        let typ = match self.reader.read_u32::<BigEndian>() {
            Ok(n) => FourCC(n),
            Err(e) => return Some(Err(e)),
        };
        if size.as_usize() == 1 {
            size = match self.reader.read_u64::<BigEndian>() {
                Ok(n) => AtomSize::ExtendedSize(n),
                Err(e) => return Some(Err(e)),
            };
        }
        let atom = Atom {
            typ: typ,
            size: size,
            offset: offset,
        };
        self.offset = Some(offset + atom.size.as_usize());
        Some(Ok(atom))
    }
}

pub trait AtomWriteExt: Write {
    fn write_four_cc(&mut self, four_cc: FourCC) -> io::Result<()> {
        self.write_u32::<BigEndian>(four_cc.0)
    }

    // Writes an atom header of the given type and size. If the data_size argument is an extended
    // size, the atom size will be written as an extended size. Otherwise, the atom size will be
    // written in the smallest form possible.
    fn write_atom_header<S: Into<AtomSize>>(&mut self, typ: FourCC, data_size: S) -> io::Result<()> {
        let data_size = data_size.into();
        let atom_size = match data_size {
            AtomSize::ExtendedSize(data_size) => AtomSize::ExtendedSize(data_size + 16),
            AtomSize::Size(data_size) => {
                if data_size > 0xFFFFFFF7 {
                    AtomSize::ExtendedSize(data_size as u64 + 16)
                } else {
                    AtomSize::Size(data_size + 8)
                }
            }
        };
        match atom_size {
            AtomSize::ExtendedSize(size) => {
                self.write_u32::<BigEndian>(1)?;
                self.write_four_cc(typ)?;
                self.write_u64::<BigEndian>(size)
            }
            AtomSize::Size(size) => {
                self.write_u32::<BigEndian>(size)?;
                self.write_four_cc(typ)
            }
        }
    }

    fn write_atom<T: AtomData + WriteData>(&mut self, atom: T) -> Result<()> {
        let mut buf = Vec::new();
        atom.write(&mut buf)?;
        self.write_atom_header(T::TYPE, buf.len())?;
        copy(&mut buf.as_slice(), self)?;
        Ok(())
    }
}

impl<W: Write> AtomWriteExt for W {}
