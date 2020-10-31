use std::io;

pub struct Bitstream<T> {
    inner: T,
    next_bits: u128,
    next_bits_length: usize,
}

impl<T: Iterator<Item = u8>> Bitstream<T> {
    pub fn new<U: IntoIterator<Item = u8, IntoIter = T>>(inner: U) -> Self {
        Self {
            inner: inner.into_iter(),
            next_bits: 0,
            next_bits_length: 0,
        }
    }

    pub fn byte_aligned(&self) -> bool {
        self.next_bits_length % 8 == 0
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
                    Some(b) => b as u128,
                    None => return false,
                };
                self.next_bits_length = 8 - n;
            }
        } else {
            self.next_bits_length -= n;
        }
        true
    }

    pub fn bits(&mut self) -> BitstreamBits<T> {
        BitstreamBits { bs: self }
    }

    pub fn next_bits(&mut self, n: usize) -> Option<u64> {
        while self.next_bits_length < n {
            let b = self.inner.next()? as u128;
            self.next_bits = (self.next_bits << 8) | b;
            self.next_bits_length += 8;
        }
        Some(((self.next_bits >> (self.next_bits_length - n)) & (0xffffffffffffffff >> (64 - n))) as u64)
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

    // Reads exactly the given number of bytes into a Vec. The bitstream must be byte aligned.
    pub fn read_bytes(&mut self, n: usize) -> io::Result<Vec<u8>> {
        if !self.byte_aligned() {
            return Err(io::Error::new(io::ErrorKind::Other, "bitstream is not byte aligned"));
        }
        let mut ret = Vec::with_capacity(n);
        let mut read = 0;
        while n > read && self.next_bits_length > 0 {
            ret.push(self.read_bits(8)? as _);
            read += 1;
        }
        ret.extend((&mut self.inner).take(n - read));
        if ret.len() < n {
            return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "unexpected end of bitstream"));
        }
        Ok(ret)
    }

    // Returns true if there is more RBSP data. This assumes that the rbsp_stop_one_bit has not
    // already been consumed and that the RBSP does not have any trailing cabac_zero_words.
    pub fn more_non_slice_rbsp_data(&mut self) -> bool {
        match self.next_bits(1) {
            None => false,
            Some(0) => true,
            Some(_) => {
                // if there's more than one byte left, this is not the rbsp_stop_one_bit
                self.next_bits_length > 8 
                    // if there's more than one set bit left, this is not the rbsp_stop_one_bit
                    || self.next_bits(self.next_bits_length) != Some(1 << (self.next_bits_length - 1))
                    // if there is additional data after the byte alignment, this is not the rbsp_stop_one_bit
                    || self.next_bits(self.next_bits_length + 1).is_some()
            }
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

pub struct BitstreamBits<'a, T> {
    bs: &'a mut Bitstream<T>,
}

impl<'a, T: Iterator<Item = u8>> Iterator for BitstreamBits<'a, T> {
    type Item = bool;

    fn next(&mut self) -> Option<Self::Item> {
        self.bs.read_bits(1).ok().map(|n| n == 1)
    }
}

pub trait Decode: Sized {
    fn decode<T: Iterator<Item = u8>>(bs: &mut Bitstream<T>) -> io::Result<Self>;
}

pub struct BitstreamWriter<T: io::Write> {
    inner: T,
    next_bits: u128,
    next_bits_length: usize,
}

impl<T: io::Write> BitstreamWriter<T> {
    pub fn new(inner: T) -> Self {
        Self {
            inner,
            next_bits: 0,
            next_bits_length: 0,
        }
    }

    pub fn byte_aligned(&self) -> bool {
        self.next_bits_length % 8 == 0
    }

    // Writes the given bits to the bitstream. If an error occurs, it is undefined how many bits
    // were actually written to the underlying bitstream.
    pub fn write_bits(&mut self, bits: u64, len: usize) -> io::Result<()> {
        self.next_bits = (self.next_bits << len) | bits as u128;
        self.next_bits_length += len;
        while self.next_bits_length >= 8 {
            let next_byte = (self.next_bits >> (self.next_bits_length - 8)) as u8;
            self.inner.write_all(&[next_byte])?;
            self.next_bits_length -= 8;
        }
        Ok(())
    }

    // Writes the remaining bits to the underlying writer if there are any, and flushes it. If the
    // bitstream is not byte-aligned, zero-bits will be appended until it is.
    pub fn flush(&mut self) -> io::Result<()> {
        if self.next_bits_length > 0 {
            let next_byte = (self.next_bits << (8 - self.next_bits_length)) as u8;
            self.inner.write_all(&[next_byte])?;
            self.next_bits_length = 0;
        }
        self.inner.flush()
    }

    pub fn inner(&self) -> &T {
        &self.inner
    }

    pub fn inner_mut(&mut self) -> &mut T {
        &mut self.inner
    }

    pub fn encode<V: Encode>(&mut self, v: &V) -> io::Result<()> {
        v.encode(self)
    }
}

impl<T: io::Write> Drop for BitstreamWriter<T> {
    fn drop(&mut self) {
        // if users need the error, they should explicitly invoke flush before dropping
        let _ = self.flush();
    }
}

pub trait Encode: Sized {
    fn encode<'a, T: io::Write>(&self, bs: &mut BitstreamWriter<T>) -> io::Result<()>;
}

impl<T: Encode> Encode for Vec<T> {
    fn encode<'a, U: io::Write>(&self, bs: &mut BitstreamWriter<U>) -> io::Result<()> {
        for v in self {
            v.encode(bs)?;
        }
        Ok(())
    }
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

#[macro_export]
macro_rules! encode {
    ($b:expr, $e:expr) => {{
        $b.encode($e)
    }};
    ($b:expr, $e:expr, $($r:expr),+) => {
        encode!($b, $e).and(encode!($b, $($r),+))
    };
}

#[cfg(test)]
mod test {
    use super::{super::syntax_elements::U2, *};

    #[test]
    fn test_decode() {
        let mut bs = Bitstream::new(vec![0x90]);
        let mut a = U2::default();
        let mut b = U2::default();
        decode!(bs, &mut a, &mut b).unwrap();
        assert_eq!(a.0, 2);
        assert_eq!(b.0, 1);
    }

    #[test]
    fn test_encode() {
        let mut b = Vec::new();
        BitstreamWriter::new(&mut b).write_bits(0x0102, 18).unwrap();
        assert_eq!(b, &[0x00, 0x40, 0x80]);
    }
}
