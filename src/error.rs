#[derive(Debug)]
pub enum Error {
    IOError(std::io::Error),
    MalformedFile(&'static str),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::IOError(err) => err.fmt(f),
            Error::MalformedFile(reason) => write!(f, "malformed file: {}", reason),
        }
    }
}

impl std::error::Error for Error {}

impl From<std::io::Error> for Error {
    fn from(err: std::io::Error) -> Error {
        Error::IOError(err)
    }
}

pub type Result<T> = std::result::Result<T, Error>;
