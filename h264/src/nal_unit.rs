use super::{decode, encode, syntax_elements::*, Bitstream, BitstreamWriter};

use std::io;

/// The first byte of each NALU contains its type. If you just need the type without decoding the
/// NALU, mask the first byte with this.
pub const NAL_UNIT_TYPE_MASK: u8 = 0x1f;

pub const NAL_UNIT_TYPE_CODED_SLICE_OF_IDR_PICTURE: u8 = 5;
pub const NAL_UNIT_TYPE_SUPPLEMENTAL_ENHANCEMENT_INFORMATION: u8 = 6;
pub const NAL_UNIT_TYPE_SEQUENCE_PARAMETER_SET: u8 = 7;
pub const NAL_UNIT_TYPE_PICTURE_PARAMETER_SET: u8 = 8;

// ITU-T H.264, 04/2017, 7.3.1
pub struct NALUnit<T> {
    pub forbidden_zero_bit: F1,
    pub nal_ref_idc: U2,
    pub nal_unit_type: U5,
    pub rbsp_byte: RBSP<T>,
}

impl<T: Clone> Clone for NALUnit<T> {
    fn clone(&self) -> Self {
        Self {
            forbidden_zero_bit: self.forbidden_zero_bit,
            nal_ref_idc: self.nal_ref_idc,
            nal_unit_type: self.nal_unit_type,
            rbsp_byte: self.rbsp_byte.clone(),
        }
    }
}

#[derive(Clone, Debug)]
pub struct RBSP<T> {
    inner: T,
}

impl<T> RBSP<T> {
    pub fn new(inner: T) -> Self {
        Self { inner }
    }

    pub fn into_inner(self) -> T {
        self.inner
    }
}

pub struct RBSPIter<T> {
    inner: T,
    zeros: usize,
}

impl<T: IntoIterator<Item = u8>> IntoIterator for RBSP<T> {
    type Item = u8;
    type IntoIter = RBSPIter<T::IntoIter>;

    fn into_iter(self) -> Self::IntoIter {
        RBSPIter {
            inner: self.inner.into_iter(),
            zeros: 0,
        }
    }
}

impl<'a, T: Iterator<Item = u8>> IntoIterator for &'a mut RBSP<T> {
    type Item = u8;
    type IntoIter = RBSPIter<&'a mut T>;

    fn into_iter(self) -> Self::IntoIter {
        RBSPIter {
            inner: &mut self.inner,
            zeros: 0,
        }
    }
}

impl<T: Iterator<Item = u8>> Iterator for RBSPIter<T> {
    type Item = u8;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            match self.inner.next() {
                Some(3) if self.zeros >= 2 => {
                    self.zeros = 0;
                    continue;
                }
                next @ Some(0) => {
                    self.zeros += 1;
                    return next;
                }
                next @ Some(_) => {
                    self.zeros = 0;
                    return next;
                }
                None => return None,
            }
        }
    }
}

/// Adds emulation prevention to RBSP data.
pub struct EmulationPrevention<T> {
    inner: T,
    next: Option<u8>,
    zeros: usize,
}

impl<T: Iterator<Item = u8>> EmulationPrevention<T> {
    pub fn new<U: IntoIterator<Item = u8, IntoIter = T>>(inner: U) -> Self {
        Self {
            inner: inner.into_iter(),
            next: None,
            zeros: 0,
        }
    }

    pub fn into_inner(self) -> T {
        self.inner
    }
}

impl<'a, T: Iterator<Item = u8>> Iterator for &mut EmulationPrevention<T> {
    type Item = u8;

    fn next(&mut self) -> Option<Self::Item> {
        match self.next.take() {
            Some(b) => {
                if b == 0 {
                    self.zeros += 1;
                }
                Some(b)
            }
            None => match self.inner.next() {
                next @ Some(0..=3) if self.zeros == 2 => {
                    // insert an emulation prevention byte
                    self.next = next;
                    self.zeros = 0;
                    Some(3)
                }
                Some(0) => {
                    self.zeros += 1;
                    Some(0)
                }
                next @ Some(_) => {
                    self.zeros = 0;
                    next
                }
                None => {
                    if self.zeros > 0 {
                        // the rbsp cannot end with a zero
                        self.zeros = 0;
                        Some(3)
                    } else {
                        None
                    }
                }
            },
        }
    }
}

impl<T: Iterator<Item = u8>> NALUnit<T> {
    pub fn decode(mut bs: Bitstream<T>) -> io::Result<Self> {
        let mut forbidden_zero_bit = F1::default();
        let mut nal_ref_idc = U2::default();
        let mut nal_unit_type = U5::default();
        decode!(bs, &mut forbidden_zero_bit, &mut nal_ref_idc, &mut nal_unit_type)?;

        if forbidden_zero_bit.0 != 0 {
            return Err(io::Error::new(io::ErrorKind::Other, "non-zero forbidden_zero_bit"));
        }

        match nal_unit_type.0 {
            14 | 20 | 21 => return Err(io::Error::new(io::ErrorKind::Other, "unsupported nal_unit_type")),
            _ => {}
        }

        Ok(Self {
            forbidden_zero_bit,
            nal_ref_idc,
            nal_unit_type,
            rbsp_byte: RBSP::new(bs.into_inner()),
        })
    }
}

impl<T: IntoIterator<Item = u8>> NALUnit<T> {
    pub fn encode<W: io::Write>(self, bs: &mut BitstreamWriter<W>) -> io::Result<()> {
        encode!(bs, &self.forbidden_zero_bit, &self.nal_ref_idc, &self.nal_unit_type)?;
        bs.flush()?;
        bs.inner_mut().write_all(&EmulationPrevention::new(self.rbsp_byte).collect::<Vec<u8>>())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_emulation_prevention() {
        let out = EmulationPrevention::new(vec![0, 0, 0, 1]).collect::<Vec<_>>();
        assert_eq!(out, vec![0, 0, 3, 0, 1]);

        let out = EmulationPrevention::new(vec![0, 0, 0, 0]).collect::<Vec<_>>();
        assert_eq!(out, vec![0, 0, 3, 0, 0, 3]);
    }
}
