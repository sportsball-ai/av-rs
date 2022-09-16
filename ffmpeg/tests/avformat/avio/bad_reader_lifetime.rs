use ffmpeg::avformat::avio::*;
use std::mem::drop;

fn main() {
    let buf = vec![];
    let r = Reader::new(&*buf).unwrap();

    // The buffer must outlive the reader that holds a reference to it. Dropping the buffer first
    // should not be allowed:
    drop(buf);
    drop(r);
}
