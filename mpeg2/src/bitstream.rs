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
        self.inner.len() - self.next_byte
    }

    pub fn wrapped_stream(&self, n: usize) -> Self {
        Self {
            inner: &self.inner[self.next_byte..(self.next_byte + n)],
            next_bit: 0,
            next_byte: 0,
        }
    }

    pub fn peek_next_byte(&self) -> usize {
        self.next_byte
    }
    pub fn peek_next_bit(&self) -> u8 {
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

    pub fn write_bit(&mut self, flag: bool) {
        if flag {
            self.inner[self.next_byte] |= 1 << (7 - self.next_bit);
        }
        self.next_byte += (self.next_bit as usize + 1) >> 3;
        self.next_bit = (self.next_bit + 1) & 0x7;
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
}
