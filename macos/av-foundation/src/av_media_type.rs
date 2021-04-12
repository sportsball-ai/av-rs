use std::os::raw::c_int;

pub enum AVMediaType {
    Audio,
    Video,
}

impl AVMediaType {
    pub(crate) fn as_sys(&self) -> c_int {
        match self {
            Self::Audio => 0,
            Self::Video => 1,
        }
    }
}
