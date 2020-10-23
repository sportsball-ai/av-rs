use std::fs::File;
use std::io;

fn main() -> io::Result<()> {
    let mut video = File::open("../../video/000.h264")?;
    let sei_timing = h264::read_first_sei_pic_timing(&mut video)?;
    println!("sei_pic_timing: {:?}", sei_timing);
    Ok(())
}
