use snafu::Snafu;

#[derive(Debug, Snafu)]
pub enum XcoderInitError {
    #[snafu(display("error (code = {code})"))]
    Unknown { code: i32 },
}
