use std::io;

pub struct Bitstream<T> {
    inner: T,
    bit_offset: usize,
}

impl<T: AsRef<[u8]>> Bitstream<T> {
    pub fn new(inner: T) -> Self {
        Self { inner, bit_offset: 0 }
    }

    pub fn bits_remaining(&self) -> usize {
        self.inner.as_ref().len() * 8 - self.bit_offset
    }

    pub fn advance_bits(&mut self, n: usize) -> bool {
        if self.bits_remaining() < n {
            return false;
        }
        self.bit_offset += n;
        return true;
    }

    pub fn next_bits(&self, n: usize) -> Option<u64> {
        if self.bits_remaining() < n {
            return None;
        }
        let mut ret = 0;
        let data = self.inner.as_ref();
        for i in 0..n {
            ret = (ret << 1) | ((data[(self.bit_offset + i) / 8] >> (8 - (self.bit_offset + i) % 8 - 1)) & 1) as u64;
        }
        Some(ret)
    }

    pub fn read_bits(&mut self, n: usize) -> io::Result<u64> {
        match self.next_bits(n) {
            Some(ret) => {
                self.bit_offset += n;
                Ok(ret)
            }
            None => Err(io::Error::new(io::ErrorKind::UnexpectedEof, "unexpected end of bitstream")),
        }
    }

    pub fn decode<V: Decode>(&mut self, v: &mut V) -> io::Result<()> {
        *v = V::decode(self)?;
        Ok(())
    }
}

pub trait Decode: Sized {
    fn decode<T: AsRef<[u8]>>(bs: &mut Bitstream<T>) -> io::Result<Self>;
}

#[macro_export]
macro_rules! decode {
    ($b:expr, $e:expr) => {{
        $b.decode($e)
    }};
    ($b:expr, $e:expr, $($r:expr),+) => {
        decode!($b, $e).and(decode!($b, $($r),+))
    };
}

#[cfg(test)]
mod test {
    use super::{super::syntax_elements::U2, *};

    #[test]
    fn test_decode() {
        let mut bs = Bitstream::new(&[0x90]);
        let mut a = U2::default();
        let mut b = U2::default();
        decode!(bs, &mut a, &mut b).unwrap();
        assert_eq!(a.0, 2);
        assert_eq!(b.0, 1);
    }
}
