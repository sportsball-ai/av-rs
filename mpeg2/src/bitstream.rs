use crate::DecodeError;
use byteorder::{BigEndian, ByteOrder};

pub struct Bitstream<'a> {
    inner: &'a [u8],
    next_bit: u8,
    next_byte: usize,
}

impl<'a> Bitstream<'a> {
    pub fn new(inner: &'a [u8]) -> Self {
        Self {
            inner,
            next_bit: 0,
            next_byte: 0,
        }
    }

    pub fn read_u8(&mut self) -> u8 {
        let ret = self.inner[self.next_byte];
        self.next_byte += 1;
        ret
    }

    pub fn peek_u8(&self) -> u8 {
        self.inner[self.next_byte]
    }

    pub fn read_u16(&mut self) -> u16 {
        let ret = BigEndian::read_u16(&self.inner[self.next_byte..]);
        self.next_byte += 2;
        ret
    }

    pub fn read_u32(&mut self) -> u32 {
        let ret = BigEndian::read_u32(&self.inner[self.next_byte..]);
        self.next_byte += 4;
        ret
    }

    pub fn read_u64(&mut self) -> u64 {
        let ret = BigEndian::read_u64(&self.inner[self.next_byte..]);
        self.next_byte += 8;
        ret
    }

    pub fn read_boolean(&mut self) -> bool {
        self.read_bit() == 1
    }

    pub fn read_bit(&mut self) -> u8 {
        let ret = (self.inner[self.next_byte] >> (7 - self.next_bit)) & 1;
        self.next_byte += (self.next_bit as usize + 1) >> 3;
        self.next_bit = (self.next_bit + 1) & 0x7;
        ret
    }

    pub fn read_n_bytes(&mut self, n: usize) -> &[u8] {
        self.next_byte += n;
        &self.inner[(self.next_byte - n)..self.next_byte]
    }

    // caller must make sure the n bits read must be within the byte that next_byte points to
    // self.next_bit + n <= 8 and n >= 1
    pub fn read_n_bits(&mut self, mut n: u8) -> u8 {
        let ret = (self.inner[self.next_byte] >> (8 - self.next_bit - n)) & (0xff >> (8 - n));
        n += self.next_bit;
        self.next_byte += n as usize >> 3;
        self.next_bit = n & 0x7;
        ret
    }

    pub fn skip_bits(&mut self, mut n: usize) {
        n += self.next_bit as usize;
        self.next_byte += n >> 3;
        self.next_bit = n as u8 & 0x7;
    }

    pub fn skip_bytes(&mut self, n: usize) {
        self.next_byte += n;
    }

    pub fn remaining_bytes(&self) -> usize {
        self.inner.len().saturating_sub(self.next_byte)
    }

    pub fn wrapped_stream(&self, n: usize) -> Self {
        Self {
            inner: &self.inner[self.next_byte..(self.next_byte + n)],
            next_bit: 0,
            next_byte: 0,
        }
    }

    pub fn get_next_byte(&self) -> u8 {
        self.inner[self.next_byte]
    }

    pub fn get_next_bit(&self) -> u8 {
        self.inner[self.next_byte] >> (7 - self.next_bit) & 1
    }

    pub fn next_byte(&self) -> usize {
        self.next_byte
    }

    pub fn next_bit(&self) -> u8 {
        self.next_bit
    }
}

pub trait Decode: Sized {
    fn decode(bs: &mut Bitstream) -> Result<Self, DecodeError>;
}

pub struct BitstreamWriter<'a> {
    inner: &'a mut [u8],
    next_bit: u8,
    next_byte: usize,
}

impl<'a> BitstreamWriter<'a> {
    pub fn new(inner: &'a mut [u8]) -> Self {
        Self {
            inner,
            next_bit: 0,
            next_byte: 0,
        }
    }

    pub fn write_boolean(&mut self, flag: bool) {
        if flag {
            self.inner[self.next_byte] |= 1 << (7 - self.next_bit);
        }
        self.next_byte += (self.next_bit as usize + 1) >> 3;
        self.next_bit = (self.next_bit + 1) & 0x7;
    }

    pub fn write_bit(&mut self, val: u8) {
        self.write_boolean(val & 1 == 1);
    }

    // Writes the lower n bits in val, n + self.next_bit <= 8
    pub fn write_n_bits(&mut self, val: u8, n: u8) {
        self.inner[self.next_byte] |= (0xff >> (8 - n) & val) << (8 - n - self.next_bit);
        self.next_byte += (self.next_bit + n) as usize >> 3;
        self.next_bit = (self.next_bit + n) & 0x7;
    }

    pub fn write_u8(&mut self, val: u8) {
        self.inner[self.next_byte] = val;
        self.next_byte += 1;
    }

    pub fn write_u16(&mut self, val: u16) {
        BigEndian::write_u16(&mut self.inner[self.next_byte..], val);
        self.next_byte += 2;
    }

    pub fn write_u32(&mut self, val: u32) {
        BigEndian::write_u32(&mut self.inner[self.next_byte..], val);
        self.next_byte += 4;
    }

    pub fn write_u64(&mut self, val: u64) {
        BigEndian::write_u64(&mut self.inner[self.next_byte..], val);
        self.next_byte += 8;
    }

    pub fn skip_n_bits(&mut self, mut n: u8) {
        n += self.next_bit;
        self.next_byte += (n as usize) >> 3;
        self.next_bit = n as u8 & 0x7;
    }

    pub fn skip_n_bytes(&mut self, n: usize) {
        self.next_byte += n;
    }

    pub fn inner_remaining(&mut self) -> &mut [u8] {
        &mut self.inner[self.next_byte..]
    }
}

#[cfg(test)]
mod tests {
    use crate::bitstream::*;

    #[test]
    fn test_bitstream_read() {
        let buf = [
            0b10110111, 0b10111010, 0x70, 0x0f, 0x47, 0x55, 0x54, 0xce, 0x3f, 0x82, 0xfa, 0x64, 0x7c, 0x16, 0xf1, 0x55, 0x54, 0xce, 0x3f,
        ];
        let mut bs = Bitstream::new(&buf[..]);
        assert!(bs.read_boolean());
        assert_eq!(bs.read_bit(), 0);
        assert_eq!(bs.read_n_bits(2), 0b11);
        assert_eq!(bs.read_n_bits(3), 0b011);
        bs.skip_bits(3);
        assert_eq!(bs.read_n_bits(6), 0b11_1010);

        assert_eq!(bs.read_u8(), 0x70);
        // assert_eq!(bs.read_u16(), (0x0f<<8) + 0x47);
        assert_eq!(bs.read_u16(), BigEndian::read_u16(&[0x0f, 0x47]));
        assert_eq!(bs.read_u32(), 0x55 << 24 | 0x54 << 16 | 0xce << 8 | 0x3f);
        bs.skip_bytes(1);
        assert_eq!(bs.remaining_bytes(), 9);
        assert_eq!(bs.read_u64(), BigEndian::read_u64(&[0xfa, 0x64, 0x7c, 0x16, 0xf1, 0x55, 0x54, 0xce]));
        bs.skip_bytes(8);
        assert_eq!(bs.remaining_bytes(), 0);
    }

    #[test]
    fn test_bitstream_write() {
        let data = [
            0x70, 0b10100100, 0b00111010, 0x0f, 0x47, 0x55, 0x54, 0xce, 0x3f, 0x00, 0x00, 0x64, 0x7c, 0x16, 0xf1, 0x55, 0x54, 0xce, 0x3f,
        ];

        let mut w = vec![0x00; data.len()];
        let mut bs = BitstreamWriter::new(&mut w);
        bs.write_u8(0x70);
        bs.write_boolean(true);
        bs.write_n_bits(0b01, 2);
        bs.skip_n_bits(2);
        bs.write_bit(1);
        bs.skip_n_bits(4);
        bs.write_n_bits(0b11101, 5);
        bs.write_boolean(false);

        bs.write_u16(0x0f << 8 | 0x47);
        bs.write_u32(0x55 << 24 | 0x54 << 16 | 0xce << 8 | 0x3f);
        bs.skip_n_bytes(2);
        bs.write_u64(0x64 << 56 | 0x7c << 48 | 0x16 << 40 | 0xf1 << 32 | 0x55 << 24 | 0x54 << 16 | 0xce << 8 | 0x3f);

        assert_eq!(&w, &data);
    }
}
