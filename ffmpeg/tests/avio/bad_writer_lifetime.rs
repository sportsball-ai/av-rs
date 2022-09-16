use ffmpeg::avio::*;
use std::mem::drop;

fn main() {
    let mut buf = vec![];
    let w = Writer::new(&mut *buf).unwrap();

    // The buffer must outlive the writer that holds a reference to it. Dropping the buffer first
    // should not be allowed:
    drop(buf);
    drop(w);
}
