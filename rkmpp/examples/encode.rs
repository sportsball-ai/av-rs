//! Encodes raw video to H.264/H.265.

use av_traits::VideoEncoder as _;
use clap::Parser;
use std::io::{Read as _, Write as _};
use std::path::PathBuf;
use std::time::{Duration, Instant};

#[derive(Parser)]
struct Args {
    #[arg(long)]
    width: u16,

    #[arg(long)]
    height: u16,

    #[arg(long)]
    fps: f64,

    #[arg(long)]
    bitrate: Option<u32>,

    #[arg(long)]
    keyframe_interval: Option<u32>,

    #[arg(long)]
    codec: rkmpp::RkMppEncoderCodec,

    #[arg(long)]
    pixel_format: rkmpp::RkMppEncoderInputFormat,

    #[arg(long)]
    input: PathBuf,

    #[arg(long)]
    output: PathBuf,
}

struct Frame {
    data: Vec<u8>,
    plane_offsets: [usize; 4],
}

impl Frame {
    fn new(width: u16, height: u16, format: rkmpp::RkMppEncoderInputFormat) -> Self {
        match format {
            rkmpp::RkMppEncoderInputFormat::Yuv420Planar => {
                let luma_dims = usize::from(width) * usize::from(height);
                // TODO: not right for odd widths.
                let chroma_dims = luma_dims >> 2;
                let plane_offsets = [0, luma_dims, luma_dims + chroma_dims, luma_dims + 2 * chroma_dims];
                Self {
                    data: vec![0; luma_dims + 2 * chroma_dims],
                    plane_offsets,
                }
            }
            _ => unimplemented!(),
        }
    }
}

impl av_traits::RawVideoFrame<u8> for Frame {
    fn samples(&self, plane: usize) -> &[u8] {
        &self.data[self.plane_offsets[plane]..self.plane_offsets[plane + 1]]
    }
}

fn main() {
    let args = Args::parse();

    let mut infile = std::fs::File::open(&args.input).unwrap();
    let mut outfile = std::fs::File::create(&args.output).unwrap();

    let config = rkmpp::RkMppEncoderConfig {
        width: args.width,
        height: args.height,
        fps: args.fps,
        bitrate: args.bitrate,
        keyframe_interval: args.keyframe_interval,
        codec: args.codec,
        input_format: args.pixel_format,
    };
    let mut encoder = rkmpp::RkMppEncoder::new(config).unwrap();

    let len = infile.metadata().unwrap().len();

    // yuck.
    let frame_len = u64::try_from(Frame::new(args.width, args.height, args.pixel_format).data.len()).unwrap();
    let frames_in = len / frame_len;
    if len % frame_len != 0 {
        panic!("Input length {len} is not an even multiple of frame length {frame_len}; check width/height/format");
    }

    let mut frames_out = 0;
    let pb =
        indicatif::ProgressBar::new(frames_in).with_style(indicatif::ProgressStyle::with_template("encoding at {msg} {elapsed} {wide_bar} {eta}").unwrap());

    let (raw_frame_tx, raw_frame_rx) = std::sync::mpsc::sync_channel(3);
    let (encoded_frame_tx, encoded_frame_rx) = std::sync::mpsc::sync_channel::<av_traits::VideoEncoderOutput<Frame>>(3);
    std::thread::scope(|s| {
        let pb = &pb;
        let before = Instant::now();
        std::thread::Builder::new()
            .name("reader".to_owned())
            .spawn_scoped(s, move || {
                let mut last = before;
                let mut reading = Duration::ZERO;
                let mut sending = Duration::ZERO;
                for _ in 0..frames_in {
                    let mut frame = Frame::new(args.width, args.height, args.pixel_format);
                    infile.read_exact(&mut frame.data[..]).unwrap();
                    let pre_send = Instant::now();
                    reading += pre_send.duration_since(last);
                    raw_frame_tx.send(frame).unwrap();
                    last = Instant::now();
                    sending += last.duration_since(pre_send);
                }
                drop(raw_frame_tx);
                pb.println(format!("read thread: reading={reading:?} sending={sending:?}"));
            })
            .unwrap();

        std::thread::Builder::new()
            .name("writer".to_owned())
            .spawn_scoped(s, move || {
                let mut last = Instant::now();
                let mut receiving = Duration::ZERO;
                let mut writing = Duration::ZERO;
                while let Ok(frame) = encoded_frame_rx.recv() {
                    let pre_write = Instant::now();
                    receiving += pre_write.duration_since(last);
                    outfile.write_all(&frame.encoded_frame.unwrap().data).unwrap();
                    last = Instant::now();
                    frames_out += 1;
                    pb.set_message(format!("{:.02}x", (frames_out as f64) / args.fps / (last - before).as_secs_f64()));
                    pb.inc(1);
                }
                let pre_drop = Instant::now();
                receiving += pre_drop.duration_since(last);
                drop(outfile);
                writing += Instant::now().duration_since(pre_drop);
                pb.println(format!("write thread: receiving={receiving:?} writing={writing:?}"));
            })
            .unwrap();

        let mut encoding = Duration::ZERO;
        let mut frame_type = av_traits::EncodedFrameType::Key; // first frame must be key.
        while let Ok(frame) = raw_frame_rx.recv() {
            let pre = Instant::now();
            let output = encoder.encode(frame, frame_type).unwrap();
            encoding += Instant::now().duration_since(pre);
            if let Some(o) = output {
                encoded_frame_tx.send(o).unwrap();
                pb.inc(1);
                frames_out += 1;
            }
            frame_type = av_traits::EncodedFrameType::Auto;
        }
        loop {
            let pre = Instant::now();
            let output = encoder.flush().unwrap();
            encoding += Instant::now().duration_since(pre);
            let Some(output) = output else {
                break;
            };
            encoded_frame_tx.send(output).unwrap();
            pb.inc(1);
            frames_out += 1;
        }
        drop(encoded_frame_tx);
        let total = Instant::now().duration_since(before);
        pb.println(format!("encode thread: encoding={encoding:?} total={total:?}"));
    });
    assert_eq!(frames_in, frames_out);
    pb.finish();
}
