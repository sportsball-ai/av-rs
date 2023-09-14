use crate::sys::*;
use std::fmt;

use crate::XMA_TRY_AGAIN;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum XlnxErrorType {
    TryAgain,
    EOS,
    SendMoreData,
    FlushAgain,
    ErrorInvalid,
    Other,
}

pub struct XlnxError {
    pub err: XlnxErrorType,
    pub message: String,
}

impl std::error::Error for XlnxError {}

impl XlnxError {
    pub fn new(xma_error: i32, message: Option<String>) -> Self {
        const TRY_AGAIN: i32 = XMA_TRY_AGAIN as i32;
        const EOS: i32 = XMA_EOS as i32;
        const SEND_MORE_DATA: i32 = XMA_SEND_MORE_DATA as i32;
        const FLUSH_AGAIN: i32 = XMA_FLUSH_AGAIN as i32;
        const ERROR_INVALID: i32 = XMA_ERROR_INVALID as i32;

        let err = match xma_error {
            TRY_AGAIN => XlnxErrorType::TryAgain,
            EOS => XlnxErrorType::EOS,
            SEND_MORE_DATA => XlnxErrorType::SendMoreData,
            FLUSH_AGAIN => XlnxErrorType::FlushAgain,
            ERROR_INVALID => XlnxErrorType::ErrorInvalid,
            _ => XlnxErrorType::Other,
        };

        let message = match message {
            Some(m) => m,
            None => String::from("error occured during xilinx operation"),
        };

        Self { err, message }
    }
}

impl fmt::Display for XlnxError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let err_msg = format!("{}: {:?}", self.message, self.err);
        write!(f, "{}", err_msg)
    }
}

impl fmt::Debug for XlnxError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "XlnxError {{ err: {:?}, message: {:?} }}", self.err, self.message,)
    }
}
