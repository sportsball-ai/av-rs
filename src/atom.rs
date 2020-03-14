use std::{io, io::{Read, Seek, SeekFrom}};

use byteorder::{BigEndian, ByteOrder, ReadBytesExt, WriteBytesExt};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
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

pub struct SectionReader<R: Read + Seek> {
    reader: R,
    begin: u64,
    end: u64,
    position: u64,
}

impl<R: Read + Seek> SectionReader<R> {
    pub fn new(reader: R, offset: usize, len: usize) -> Self {
        Self{
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
        Ok(self.position)
    }
}

#[derive(Clone)]
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
}

pub struct AtomReader<R: Read + Seek> {
    offset: usize,
    reader: R,
}

impl<R: Read + Seek> AtomReader<R> {
    pub fn new(reader: R) -> Self {
        Self{
            offset: 0,
            reader: reader,
        }
    }
}

impl<R: Read + Seek> Iterator for AtomReader<R> {
    type Item = io::Result<Atom>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Err(e) = self.reader.seek(SeekFrom::Start(self.offset as u64)) {
            return Some(Err(e));
        }
        let mut size = match self.reader.read_u32::<BigEndian>() {
            Ok(n) => AtomSize::Size(n),
            Err(e) => return match e.kind() {
                io::ErrorKind::UnexpectedEof => None,
                _ => Some(Err(e)),
            },
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
        let atom = Atom{
            typ: typ,
            size: size,
            offset: self.offset,
        };
        self.offset += atom.size.as_usize();
        Some(Ok(atom))
    }
}
