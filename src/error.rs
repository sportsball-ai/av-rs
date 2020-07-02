use std::fmt::Display;
use std::io;

use serde::de::Error as SerdeDeError;
use serde::ser::Error as SerdeSerError;

#[derive(Debug)]
pub enum Error {
    IOError(io::Error),
    MalformedFile(&'static str),
    DeserializationError(String),
    SerializationError(String),
    Other(&'static str),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::IOError(err) => err.fmt(f),
            Error::MalformedFile(reason) => write!(f, "malformed file: {}", reason),
            Error::DeserializationError(message) => write!(f, "deserialization error: {}", message),
            Error::SerializationError(message) => write!(f, "serialization error: {}", message),
            Error::Other(reason) => write!(f, "{}", reason),
        }
    }
}

impl std::error::Error for Error {}

impl From<io::Error> for Error {
    fn from(err: io::Error) -> Error {
        Error::IOError(err)
    }
}

impl SerdeDeError for Error {
    fn custom<T: Display>(msg: T) -> Self {
        Error::DeserializationError(msg.to_string())
    }
}

impl SerdeSerError for Error {
    fn custom<T: Display>(msg: T) -> Self {
        Error::SerializationError(msg.to_string())
    }
}

pub type Result<T> = std::result::Result<T, Error>;
