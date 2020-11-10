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
    pub const AVC1: FourCC = FourCC(0x6176_6331);
    pub const AVC3: FourCC = FourCC(0x6176_6333);
    pub const AVCC: FourCC = FourCC(0x6176_6343);
    pub const BLOC: FourCC = FourCC(0x626C_6F63);
    pub const CENC: FourCC = FourCC(0x6365_6e63);
    pub const CO64: FourCC = FourCC(0x636f_3634);
    pub const CTTS: FourCC = FourCC(0x6374_7473);
    pub const DATA: FourCC = FourCC(0x6461_7461);
    pub const DINF: FourCC = FourCC(0x6469_6e66);
    pub const EDTS: FourCC = FourCC(0x6564_7473);
    pub const ELNG: FourCC = FourCC(0x656c_6e67);
    pub const ELST: FourCC = FourCC(0x656c_7374);
    pub const EMSG: FourCC = FourCC(0x656d_7367);
    pub const ENCA: FourCC = FourCC(0x656e_6361);
    pub const ENCV: FourCC = FourCC(0x656e_6376);
    pub const ESDS: FourCC = FourCC(0x6573_6473);
    pub const FREE: FourCC = FourCC(0x6672_6565);
    pub const FRMA: FourCC = FourCC(0x6672_6d61);
    pub const FTYP: FourCC = FourCC(0x6674_7970);
    pub const GMHD: FourCC = FourCC(0x676d_6864);
    pub const HDLR: FourCC = FourCC(0x6864_6c72);
    pub const HEV1: FourCC = FourCC(0x6865_7631);
    pub const HINT: FourCC = FourCC(0x6869_6e74);
    pub const HVC1: FourCC = FourCC(0x6876_6331);
    pub const HVCC: FourCC = FourCC(0x6876_6343);
    pub const ILST: FourCC = FourCC(0x696c_7374);
    pub const IODS: FourCC = FourCC(0x696f_6473);
    pub const KEYS: FourCC = FourCC(0x6b65_7973);
    pub const MDAT: FourCC = FourCC(0x6d64_6174);
    pub const MDHD: FourCC = FourCC(0x6d64_6864);
    pub const MDIA: FourCC = FourCC(0x6d64_6961);
    pub const MECO: FourCC = FourCC(0x6d65_636f);
    pub const MEHD: FourCC = FourCC(0x6d65_6864);
    pub const META: FourCC = FourCC(0x6d65_7461);
    pub const MFHD: FourCC = FourCC(0x6d66_6864);
    pub const MFRA: FourCC = FourCC(0x6d66_7261);
    pub const MINF: FourCC = FourCC(0x6d69_6e66);
    pub const MOOF: FourCC = FourCC(0x6d6f_6f66);
    pub const MOOV: FourCC = FourCC(0x6d6f_6f76);
    pub const MP4A: FourCC = FourCC(0x6d70_3461);
    pub const MP4V: FourCC = FourCC(0x6d70_3476);
    pub const MVEX: FourCC = FourCC(0x6d76_6578);
    pub const MVHD: FourCC = FourCC(0x6d76_6864);
    pub const PASP: FourCC = FourCC(0x7061_7370);
    pub const PDIN: FourCC = FourCC(0x7064_696e);
    pub const PRFT: FourCC = FourCC(0x7072_6674);
    pub const PSSH: FourCC = FourCC(0x7073_7368);
    pub const SAIO: FourCC = FourCC(0x7361_696f);
    pub const SAIZ: FourCC = FourCC(0x7361_697a);
    pub const SBGP: FourCC = FourCC(0x7362_6770);
    pub const SCHI: FourCC = FourCC(0x7363_6869);
    pub const SCHM: FourCC = FourCC(0x7363_686d);
    pub const SDTP: FourCC = FourCC(0x7364_7470);
    pub const SEIG: FourCC = FourCC(0x7365_6967);
    pub const SGPD: FourCC = FourCC(0x7367_7064);
    pub const SIDX: FourCC = FourCC(0x7369_6478);
    pub const SINF: FourCC = FourCC(0x7369_6e66);
    pub const SKIP: FourCC = FourCC(0x736b_6970);
    pub const SMHD: FourCC = FourCC(0x736d_6864);
    pub const SOUN: FourCC = FourCC(0x736f_756e);
    pub const SSIX: FourCC = FourCC(0x7373_6978);
    pub const STBL: FourCC = FourCC(0x7374_626c);
    pub const STCO: FourCC = FourCC(0x7374_636f);
    pub const STSC: FourCC = FourCC(0x7374_7363);
    pub const STSD: FourCC = FourCC(0x7374_7364);
    pub const STSS: FourCC = FourCC(0x7374_7373);
    pub const STSZ: FourCC = FourCC(0x7374_737a);
    pub const STTS: FourCC = FourCC(0x7374_7473);
    pub const STYP: FourCC = FourCC(0x7374_7970);
    pub const TENC: FourCC = FourCC(0x7465_6e63);
    pub const TFDT: FourCC = FourCC(0x7466_6474);
    pub const TFHD: FourCC = FourCC(0x7466_6864);
    pub const TKHD: FourCC = FourCC(0x746b_6864);
    pub const TRAF: FourCC = FourCC(0x7472_6166);
    pub const TRAK: FourCC = FourCC(0x7472_616b);
    pub const TREF: FourCC = FourCC(0x7472_6566);
    pub const TREX: FourCC = FourCC(0x7472_6578);
    pub const TRUN: FourCC = FourCC(0x7472_756e);
    pub const UDTA: FourCC = FourCC(0x7564_7461);
    pub const UUID: FourCC = FourCC(0x7575_6964);
    pub const VIDE: FourCC = FourCC(0x7669_6465);
    pub const VMHD: FourCC = FourCC(0x766d_6864);
    pub const WIDE: FourCC = FourCC(0x7769_6465);
}

impl FourCC {
    #[allow(clippy::should_implement_trait)]
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
        if s <= 0xffff_ffff {
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
            reader,
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
        Self { offset: None, reader }
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
            Err(e) => {
                return match e.kind() {
                    io::ErrorKind::UnexpectedEof if size.as_usize() == 0 => None,
                    _ => Some(Err(e)),
                }
            }
        };
        if size.as_usize() == 1 {
            size = match self.reader.read_u64::<BigEndian>() {
                Ok(n) => AtomSize::ExtendedSize(n),
                Err(e) => return Some(Err(e)),
            };
        }
        let atom = Atom { typ, size, offset };
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
                if data_size > 0xFFFF_FFF7 {
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
