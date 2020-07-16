use super::{Bitstream, Decode};

use std::io;

// ITU-T H.264, 04/2017, 7.2
macro_rules! define_syntax_element_u {
    ($e:ident, $t:tt, $n:literal) => {
        #[derive(Clone, Copy, Debug, Default)]
        pub struct $e(pub $t);

        impl Decode for $e {
            fn decode<T: AsRef<[u8]>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
                Ok(Self(bs.read_bits($n)? as _))
            }
        }
    };
}

define_syntax_element_u!(U1, u8, 1);
define_syntax_element_u!(U2, u8, 2);
define_syntax_element_u!(U3, u8, 3);
define_syntax_element_u!(U4, u8, 4);
define_syntax_element_u!(U5, u8, 5);
define_syntax_element_u!(U6, u8, 6);
define_syntax_element_u!(U7, u8, 7);
define_syntax_element_u!(U8, u8, 8);
define_syntax_element_u!(U16, u16, 16);
define_syntax_element_u!(U32, u32, 32);
define_syntax_element_u!(U48, u64, 48);

// ITU-T H.264, 04/2017, 7.2
define_syntax_element_u!(F1, u8, 1);

// ITU-T H.264, 04/2017, 7.2 / 9.1
#[derive(Debug, Default, Clone, Copy)]
pub struct UE(pub u64);

impl Decode for UE {
    fn decode<T: AsRef<[u8]>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let mut leading_zero_bits = 0;
        while bs.read_bits(1)? == 0 {
            leading_zero_bits += 1;
        }
        Ok(Self(bs.read_bits(leading_zero_bits)? + (1 << leading_zero_bits) - 1))
    }
}

#[derive(Debug, Default, Clone, Copy)]
pub struct SE(pub i64);

impl Decode for SE {
    fn decode<T: AsRef<[u8]>>(bs: &mut Bitstream<T>) -> io::Result<Self> {
        let ue = UE::decode(bs)?;
        let mut value = ((ue.0 + 1) >> 1) as i64;
        if (ue.0 & 1) == 0 {
            value = -value;
        }
        Ok(Self(value))
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_ue() {
        {
            let mut bs = Bitstream::new(&[0x00]);
            assert_eq!(UE::decode(&mut bs).is_err(), true);
        }

        {
            let mut bs = Bitstream::new(&[0x80]);
            assert_eq!(UE::decode(&mut bs).unwrap().0, 0);
        }

        {
            let mut bs = Bitstream::new(&[0x40]);
            assert_eq!(UE::decode(&mut bs).unwrap().0, 1);
        }

        {
            let mut bs = Bitstream::new(&[0x60]);
            assert_eq!(UE::decode(&mut bs).unwrap().0, 2);
        }

        {
            let mut bs = Bitstream::new(&[0x20]);
            assert_eq!(UE::decode(&mut bs).unwrap().0, 3);
        }

        {
            let mut bs = Bitstream::new(&[0x28]);
            assert_eq!(UE::decode(&mut bs).unwrap().0, 4);
        }
    }

    #[test]
    fn test_se() {
        {
            let mut bs = Bitstream::new(&[0x00]);
            assert_eq!(SE::decode(&mut bs).is_err(), true);
        }

        {
            let mut bs = Bitstream::new(&[0x80]);
            assert_eq!(SE::decode(&mut bs).unwrap().0, 0);
        }

        {
            let mut bs = Bitstream::new(&[0x40]);
            assert_eq!(SE::decode(&mut bs).unwrap().0, 1);
        }

        {
            let mut bs = Bitstream::new(&[0x60]);
            assert_eq!(SE::decode(&mut bs).unwrap().0, -1);
        }

        {
            let mut bs = Bitstream::new(&[0x20]);
            assert_eq!(SE::decode(&mut bs).unwrap().0, 2);
        }

        {
            let mut bs = Bitstream::new(&[0x28]);
            assert_eq!(SE::decode(&mut bs).unwrap().0, -2);
        }
    }
}
