use std::fs::File;
use std::io;

fn main() -> io::Result<()> {
    let mut video = File::open("../../video/000.h264")?;
    for timings in h264::read_sei_timings(&mut video) {
        println!("sei pic_timing: {:?}", timings);
    }
    Ok(())
}
