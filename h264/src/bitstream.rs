use std::io;

pub struct Bitstream<T> {
    inner: T,
    next_bits: u128,
    next_bits_length: usize,
}

impl<'a, T: Iterator<Item = &'a u8>> Bitstream<T> {
    pub fn new<U: IntoIterator<Item = &'a u8, IntoIter = T>>(inner: U) -> Self {
        Self {
            inner: inner.into_iter(),
            next_bits: 0,
            next_bits_length: 0,
        }
    }

    pub fn advance_bits(&mut self, mut n: usize) -> bool {
        if n > self.next_bits_length {
            n -= self.next_bits_length;
            self.next_bits_length = 0;
            for _ in 0..(n / 8) {
                if self.inner.next().is_none() {
                    return false;
                }
            }
            n = n % 8;
            if n > 0 {
                self.next_bits = match self.inner.next() {
                    Some(b) => *b as u128,
                    None => return false,
                };
                self.next_bits_length = 8 - n;
            }
        } else {
            self.next_bits_length -= n;
        }
        return true;
    }

    pub fn next_bits(&mut self, n: usize) -> Option<u64> {
        while self.next_bits_length < n {
            let b = *self.inner.next()? as u128;
            self.next_bits = (self.next_bits << 8) | b;
            self.next_bits_length += 8;
        }
        return Some(((self.next_bits >> (self.next_bits_length - n)) & (0xffffffffffffffff >> (64 - n))) as u64);
    }

    pub fn read_bits(&mut self, n: usize) -> io::Result<u64> {
        match self.next_bits(n) {
            Some(ret) => {
                self.next_bits_length -= n;
                Ok(ret)
            }
            None => Err(io::Error::new(io::ErrorKind::UnexpectedEof, "unexpected end of bitstream")),
        }
    }

    pub fn into_inner(self) -> T {
        self.inner
    }

    pub fn decode<V: Decode>(&mut self, v: &mut V) -> io::Result<()> {
        *v = V::decode(self)?;
        Ok(())
    }
}

pub trait Decode: Sized {
    fn decode<'a, T: Iterator<Item = &'a u8>>(bs: &mut Bitstream<T>) -> io::Result<Self>;
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
