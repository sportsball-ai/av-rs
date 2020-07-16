use std::iter::Iterator;

pub mod bitstream;
pub use bitstream::*;

pub mod sequence_parameter_set;
pub use sequence_parameter_set::*;

pub mod nal_unit;
pub use nal_unit::*;

pub mod syntax_elements;
pub use syntax_elements::*;

pub struct AVCCIter<'a> {
    buf: &'a [u8],
    nalu_length_size: usize,
}

pub fn iterate_avcc<'a, T: AsRef<[u8]>>(buf: &'a T, nalu_length_size: usize) -> AVCCIter<'a> {
    AVCCIter {
        buf: buf.as_ref(),
        nalu_length_size,
    }
}

impl<'a> Iterator for AVCCIter<'a> {
    type Item = &'a [u8];

    fn next(&mut self) -> Option<Self::Item> {
        if self.buf.len() < self.nalu_length_size {
            return None;
        }

        let mut len = 0;
        for i in 0..self.nalu_length_size {
            len = len << 8 | self.buf[i] as usize;
        }

        if self.buf.len() < self.nalu_length_size + len {
            return None;
        }

        let ret = &self.buf[self.nalu_length_size..self.nalu_length_size + len];
        self.buf = &self.buf[self.nalu_length_size + len..];
        Some(ret)
    }
}

pub struct AnnexBIter<'a> {
    buf: &'a [u8],
}

pub fn iterate_annex_b<'a, T: AsRef<[u8]>>(buf: &'a T) -> AnnexBIter<'a> {
    AnnexBIter { buf: buf.as_ref() }
}

impl<'a> Iterator for AnnexBIter<'a> {
    type Item = &'a [u8];

    fn next(&mut self) -> Option<Self::Item> {
        let mut pos = 0;
        let mut len = self.buf.len();
        loop {
            loop {
                if len == 0 || self.buf[pos] != 0 {
                    return None;
                } else if len >= 3 && self.buf[pos + 0] == 0 && self.buf[pos + 1] == 0 && self.buf[pos + 2] == 1 {
                    break;
                }
                pos += 1;
                len -= 1;
            }

            pos += 3;
            len -= 3;

            let nalu = pos;

            loop {
                if len == 0 || (len >= 3 && self.buf[pos + 0] == 0 && self.buf[pos + 1] == 0 && self.buf[pos + 2] <= 1) {
                    let ret = &self.buf[nalu..pos];
                    self.buf = &self.buf[pos..];
                    return Some(ret);
                }
                pos += 1;
                len -= 1;
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_iterate_annex_b() {
        let data = &[0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x01, 0x04];
        let expected: Vec<&[u8]> = vec![&[0x01, 0x02, 0x03], &[0x04]];
        assert_eq!(expected, iterate_annex_b(&data).collect::<Vec<&[u8]>>());
    }

    #[test]
    fn test_iterate_avcc() {
        let data = &[0x00, 0x00, 0x00, 0x03, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x01, 0x04];
        let expected: Vec<&[u8]> = vec![&[0x01, 0x02, 0x03], &[0x04]];
        assert_eq!(expected, iterate_avcc(&data, 4).collect::<Vec<&[u8]>>());
    }
}
