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

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("plugin {plugin:?} gave a malformed response")]
    MalformedPluginResponse { plugin: XrmPlugin },
    #[error("internal Xilinx error {source}")]
    XlnxError {
        #[from]
        source: XlnxError,
    },
    #[error("error loading dynamic library {source}")]
    Libloading {
        #[from]
        source: libloading::Error,
    },
    #[error("XRM API failed to release reserved compute unit pool")]
    FailedToReleaseCuPool { plugin: XrmPlugin },
    #[error("XRM API failed to release reserved compute units")]
    FailedToReleaseCu { plugin: XrmPlugin },
    #[error("failed to reserve {plugin:?} compute unit pool on Xilinx hardware")]
    ReserveCuPoolError { plugin: XrmPlugin },
    #[error("failed to reserve {plugin:?} compute units on Xilinx hardware for reserve_id {reserve_id:?} and device_id {device_id:?} xilinx error: {xlnx_error_code}")]
    ReserveCuError {
        plugin: XrmPlugin,
        reserve_id: Option<u64>,
        device_id: Option<u32>,
        xlnx_error_code: i32,
    },
    #[error("required parameter(s) not provided: {parameter_names}")]
    RequiredParameterMissing { parameter_names: &'static str },
    #[error("unable to create session for {plugin:?}")]
    SessionCreateFailed { plugin: XrmPlugin },
    #[error("invalid profile number {number} for {plugin:?} using codec {codec:?}")]
    InvalidCodecProfile { plugin: XrmPlugin, number: i32, codec: XrmCodec },
    #[error("invalid codec level value {level} using codec {codec:?} for {plugin:?}")]
    InvalidCodecLevel { plugin: XrmPlugin, level: i32, codec: XrmCodec },
    #[error("invalid codec id {codec} for {plugin:?}")]
    InvalidCodecId { plugin: XrmPlugin, codec: i32 },
    #[error("no device found with device id {device_id}")]
    DeviceNotFound { device_id: i32 },
    #[error("input str {input_str} exceeds output buffer size of {out_buf_size}")]
    InputStrExceedsOutputBufferSize { out_buf_size: usize, input_str: String },
}

#[derive(Debug)]
pub enum XrmPlugin {
    Decoder,
    Encoder,
    Scaler,
    /// AKA Encoder and Decoder used together
    Transcoder,
}

#[derive(Debug)]
pub enum XrmCodec {
    Hevc,
    H264,
}
