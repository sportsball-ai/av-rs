use std::io::Write;

use byteorder::{BigEndian, WriteBytesExt};
use serde::ser::{self, Impossible, Serialize};

use super::error::{Error, Result};

pub struct Serializer<W: Write> {
    writer: W,
}

impl<W: Write> Serializer<W> {
    pub fn new(writer: W) -> Self {
        Serializer { writer: writer }
    }
}

impl<'a, W: Write> ser::Serializer for &'a mut Serializer<W> {
    type Ok = ();
    type Error = Error;

    type SerializeSeq = Self;
    type SerializeTuple = Self;
    type SerializeTupleStruct = Self;
    type SerializeTupleVariant = Impossible<(), Error>;
    type SerializeMap = Impossible<(), Error>;
    type SerializeStruct = Self;
    type SerializeStructVariant = Impossible<(), Error>;

    fn serialize_bool(self, _v: bool) -> Result<()> {
        Err(Error::Other("unsupported data type: bool"))
    }

    fn serialize_i8(self, v: i8) -> Result<()> {
        self.writer.write_i8(v)?;
        Ok(())
    }

    fn serialize_i16(self, v: i16) -> Result<()> {
        self.writer.write_i16::<BigEndian>(v)?;
        Ok(())
    }

    fn serialize_i32(self, v: i32) -> Result<()> {
        self.writer.write_i32::<BigEndian>(v)?;
        Ok(())
    }

    fn serialize_i64(self, v: i64) -> Result<()> {
        self.writer.write_i64::<BigEndian>(v)?;
        Ok(())
    }

    fn serialize_u8(self, v: u8) -> Result<()> {
        self.writer.write_u8(v)?;
        Ok(())
    }

    fn serialize_u16(self, v: u16) -> Result<()> {
        self.writer.write_u16::<BigEndian>(v)?;
        Ok(())
    }

    fn serialize_u32(self, v: u32) -> Result<()> {
        self.writer.write_u32::<BigEndian>(v)?;
        Ok(())
    }

    fn serialize_u64(self, v: u64) -> Result<()> {
        self.writer.write_u64::<BigEndian>(v)?;
        Ok(())
    }

    fn serialize_f32(self, _v: f32) -> Result<()> {
        Err(Error::Other("unsupported data type: f32"))
    }

    fn serialize_f64(self, _v: f64) -> Result<()> {
        Err(Error::Other("unsupported data type: f64"))
    }

    fn serialize_char(self, _v: char) -> Result<()> {
        Err(Error::Other("unsupported data type: char"))
    }

    fn serialize_str(self, _v: &str) -> Result<()> {
        Err(Error::Other("unsupported data type: str"))
    }

    fn serialize_bytes(self, _v: &[u8]) -> Result<()> {
        Err(Error::Other("unsupported data type: bytes"))
    }

    fn serialize_none(self) -> Result<()> {
        Err(Error::Other("unsupported data type: none"))
    }

    fn serialize_some<T: ?Sized + Serialize>(self, value: &T) -> Result<()> {
        value.serialize(self)
    }

    fn serialize_unit(self) -> Result<()> {
        Err(Error::Other("unsupported data type: unit"))
    }

    fn serialize_unit_struct(self, _name: &'static str) -> Result<()> {
        Err(Error::Other("unsupported data type: unit struct"))
    }

    fn serialize_unit_variant(self, _name: &'static str, _variant_index: u32, _variant: &'static str) -> Result<()> {
        Err(Error::Other("unsupported data type: unit variant"))
    }

    fn serialize_newtype_struct<T: ?Sized + Serialize>(self, _name: &'static str, _value: &T) -> Result<()> {
        Err(Error::Other("unsupported data type: newtype struct"))
    }

    fn serialize_newtype_variant<T: ?Sized + Serialize>(self, _name: &'static str, _variant_index: u32, _variant: &'static str, _value: &T) -> Result<()> {
        Err(Error::Other("unsupported data type: newtype variant"))
    }

    fn serialize_seq(self, len: Option<usize>) -> Result<Self::SerializeSeq> {
        self.serialize_u32(len.ok_or(Error::SerializationError("unknown sequence length".to_string()))? as _)?;
        Ok(self)
    }

    fn serialize_tuple(self, _len: usize) -> Result<Self::SerializeTuple> {
        Ok(self)
    }

    fn serialize_tuple_struct(self, _name: &'static str, _len: usize) -> Result<Self::SerializeTupleStruct> {
        Ok(self)
    }

    fn serialize_tuple_variant(self, _name: &'static str, _variant_index: u32, _variant: &'static str, _len: usize) -> Result<Self::SerializeTupleVariant> {
        Err(Error::Other("unsupported data type: tuple variant"))
    }

    fn serialize_map(self, _len: Option<usize>) -> Result<Self::SerializeMap> {
        Err(Error::Other("unsupported data type: map"))
    }

    fn serialize_struct(self, _name: &'static str, _len: usize) -> Result<Self::SerializeStruct> {
        Ok(self)
    }

    fn serialize_struct_variant(self, _name: &'static str, _variant_index: u32, _variant: &'static str, _len: usize) -> Result<Self::SerializeStructVariant> {
        Err(Error::Other("unsupported data type: struct variant"))
    }
}

impl<'a, W: Write> ser::SerializeSeq for &'a mut Serializer<W> {
    type Ok = ();
    type Error = Error;

    fn serialize_element<T: ?Sized + Serialize>(&mut self, value: &T) -> Result<()> {
        value.serialize(&mut **self)
    }

    fn end(self) -> Result<()> {
        Ok(())
    }
}

impl<'a, W: Write> ser::SerializeTuple for &'a mut Serializer<W> {
    type Ok = ();
    type Error = Error;

    fn serialize_element<T: ?Sized + Serialize>(&mut self, value: &T) -> Result<()> {
        value.serialize(&mut **self)
    }

    fn end(self) -> Result<()> {
        Ok(())
    }
}

impl<'a, W: Write> ser::SerializeTupleStruct for &'a mut Serializer<W> {
    type Ok = ();
    type Error = Error;

    fn serialize_field<T: ?Sized + Serialize>(&mut self, value: &T) -> Result<()> {
        value.serialize(&mut **self)
    }

    fn end(self) -> Result<()> {
        Ok(())
    }
}

impl<'a, W: Write> ser::SerializeStruct for &'a mut Serializer<W> {
    type Ok = ();
    type Error = Error;

    fn serialize_field<T: ?Sized + Serialize>(&mut self, _key: &'static str, value: &T) -> Result<()> {
        value.serialize(&mut **self)
    }

    fn end(self) -> Result<()> {
        Ok(())
    }
}
