use av_traits::{EncodedFrameType, EncodedVideoFrame, RawVideoFrame, VideoEncoder, VideoEncoderOutput};
use nix::{
    fcntl::{open, OFlag},
    sys::stat::Mode,
};
use snafu::Snafu;
use std::{collections::VecDeque, fs::File, os::unix::io::FromRawFd, path::Path};
use v4l2r::{
    bindings, ioctl,
    memory::{MemoryType, MmapHandle},
    Format, PixelFormat, QueueType,
};

#[derive(Debug, Snafu)]
pub enum V4L2EncoderError {
    #[snafu(context(false), display("io error: {source}"))]
    IoError { source: std::io::Error },
    #[snafu(context(false), display("error setting format: {source}"))]
    SetFormatError { source: ioctl::SFmtError },
    #[snafu(context(false), display("error getting format: {source}"))]
    GetFormatError { source: ioctl::GFmtError },
    #[snafu(context(false), display("error starting stream: {source}"))]
    StreamOnError { source: ioctl::StreamOnError },
    #[snafu(context(false), display("error requesting buffers: {source}"))]
    RequestBuffersError { source: ioctl::ReqbufsError },
    #[snafu(context(false), display("error querying buffer: {source}"))]
    QueryBufferError { source: ioctl::QueryBufError<ioctl::QueryBuffer> },
    #[snafu(context(false), display("error mapping memory: {source}"))]
    MmapError { source: ioctl::MmapError },
    #[snafu(context(false), display("error dequeuing buffer: {source}"))]
    DequeueBufferError { source: ioctl::DqBufError<ioctl::V4l2Buffer> },
    #[snafu(context(false), display("error queuing buffer: {source}"))]
    QueueBufferError { source: ioctl::QBufError<()> },
    #[snafu(context(false), display("error getting or setting parameters: {source}"))]
    ParametersError { source: ioctl::GParmError },
    #[snafu(context(false), display("error getting or setting controls: {source}"))]
    ControlError { source: ioctl::GCtrlError },
    #[snafu(display("no available device"))]
    NoAvailableDevice,
    #[snafu(display("got unexpected plane layout"))]
    UnexpectedPlaneLayout,
}

type Result<T> = core::result::Result<T, V4L2EncoderError>;

pub struct V4L2Encoder<F> {
    config: V4L2EncoderConfig,
    fd: File,
    capture_mappings: Vec<ioctl::PlaneMapping>,
    output_mappings: Vec<ioctl::PlaneMapping>,
    output_format: Format,
    pending_frames: VecDeque<F>,
    available_output_buffers: Vec<usize>,
    available_capture_buffers: Vec<usize>,
}

impl<F> Drop for V4L2Encoder<F> {
    fn drop(&mut self) {
        let _ = ioctl::streamoff(&self.fd, QueueType::VideoOutputMplane);
        let _ = ioctl::streamoff(&self.fd, QueueType::VideoCaptureMplane);
        self.capture_mappings.clear();
        self.output_mappings.clear();
        let _ = ioctl::reqbufs::<()>(&self.fd, QueueType::VideoOutputMplane, MemoryType::Mmap, 0);
        let _ = ioctl::reqbufs::<()>(&self.fd, QueueType::VideoCaptureMplane, MemoryType::Mmap, 0);
    }
}

#[derive(Clone, Copy, Debug)]
pub enum V4L2EncoderInputFormat {
    Yuv420Planar,
    Bgra,
}

impl V4L2EncoderInputFormat {
    fn planes(&self) -> usize {
        match self {
            Self::Yuv420Planar => 3,
            Self::Bgra => 1,
        }
    }

    fn pixel_format(&self) -> PixelFormat {
        PixelFormat::from_u32(match self {
            Self::Yuv420Planar => 0x32315559,
            Self::Bgra => 0x34524742,
        })
    }
}

#[derive(Clone, Debug)]
pub struct V4L2EncoderConfig {
    pub height: u16,
    pub width: u16,
    pub fps: f64,
    pub bitrate: Option<u32>,
    pub keyframe_interval: Option<u32>,
    pub input_format: V4L2EncoderInputFormat,
}

const H264_PIXELFORMAT: PixelFormat = PixelFormat::from_u32(0x34363248);

// XXX: There's a really counterintuitive convention used by the V4L2 API: The output of the
// encoder is the "capture" queue, while the input to the encoder is the "output" queue.
impl<F> V4L2Encoder<F> {
    fn try_open_device<P: AsRef<Path>>(path: P, input_format: V4L2EncoderInputFormat) -> Option<File> {
        let fd = unsafe { File::from_raw_fd(open(path.as_ref().as_os_str(), OFlag::O_RDWR | OFlag::O_CLOEXEC, Mode::empty()).ok()?) };

        let caps: ioctl::Capability = ioctl::querycap(&fd).ok()?;
        if !caps.capabilities.contains(ioctl::Capabilities::VIDEO_M2M_MPLANE) {
            return None;
        }

        ioctl::reqbufs::<()>(&fd, QueueType::VideoOutputMplane, MemoryType::Mmap, 0).ok()?;
        ioctl::reqbufs::<()>(&fd, QueueType::VideoCaptureMplane, MemoryType::Mmap, 0).ok()?;

        let mut capture_formats = ioctl::FormatIterator::new(&fd, QueueType::VideoCaptureMplane);
        if !capture_formats.any(|fmt| fmt.pixelformat == H264_PIXELFORMAT) {
            return None;
        }

        let mut output_formats = ioctl::FormatIterator::new(&fd, QueueType::VideoOutputMplane);
        let input_pixel_format = input_format.pixel_format();
        if !output_formats.any(|fmt| fmt.pixelformat == input_pixel_format) {
            return None;
        }

        Some(fd)
    }

    pub fn new(config: V4L2EncoderConfig) -> Result<Self> {
        let paths = std::fs::read_dir("/dev")?;

        let mut fd = match paths
            .into_iter()
            .find_map(|path| path.map(|path| Self::try_open_device(path.path(), config.input_format)).transpose())
        {
            Some(r) => r?,
            None => return Err(V4L2EncoderError::NoAvailableDevice),
        };

        let output_format = Format {
            width: config.width as _,
            height: config.height as _,
            pixelformat: config.input_format.pixel_format(),
            ..Default::default()
        };
        let output_format: Format = ioctl::s_fmt(&mut fd, (QueueType::VideoOutputMplane, &output_format))?;

        let mut capture_format: Format = ioctl::g_fmt(&fd, QueueType::VideoCaptureMplane)?;
        capture_format.pixelformat = H264_PIXELFORMAT;
        capture_format.width = config.width as _;
        capture_format.height = config.height as _;
        let _: Format = ioctl::s_fmt(&mut fd, (QueueType::VideoCaptureMplane, &capture_format))?;

        // set the fps if possible
        {
            let mut parm: bindings::v4l2_streamparm = ioctl::g_parm(&fd, QueueType::VideoOutputMplane)?;
            let output_parm = unsafe { &mut parm.parm.output };
            if (output_parm.capability & bindings::V4L2_CAP_TIMEPERFRAME) != 0 {
                let fps_denominator = if config.fps.fract() == 0.0 {
                    1000
                } else {
                    // a denominator of 1001 for 29.97 or 59.94 is more
                    // conventional
                    1001
                };
                let fps_numerator = (config.fps * fps_denominator as f64).round() as _;
                output_parm.timeperframe.numerator = fps_denominator;
                output_parm.timeperframe.denominator = fps_numerator;
                let _: bindings::v4l2_streamparm = ioctl::s_parm(&fd, parm)?;
            }
        }

        if let Some(bitrate) = config.bitrate {
            ioctl::s_ctrl(&fd, bindings::V4L2_CID_MPEG_VIDEO_BITRATE, bitrate as _)?;
        }

        if let Some(interval) = config.keyframe_interval {
            ioctl::s_ctrl(&fd, bindings::V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, interval as _)?;
        }

        // We'll make sure the encoder always has 2 frames to work on before we block for output.
        // Experimentally, this performs 50% faster than using 1 set of buffers and using 3 has no
        // benefit over 2. Note that this currently means the encoder has a latency of 2 frames.
        const N_BUFFERS: usize = 2;

        ioctl::reqbufs(&fd, QueueType::VideoOutputMplane, MemoryType::Mmap, N_BUFFERS as _)?;
        let output_mappings = (0..N_BUFFERS)
            .map(|i| -> Result<_> {
                let buf: ioctl::QueryBuffer = ioctl::querybuf(&fd, QueueType::VideoOutputMplane, i)?;
                Ok(ioctl::mmap(&fd, buf.planes[0].mem_offset, buf.planes[0].length)?)
            })
            .collect::<Result<Vec<_>>>()?;

        ioctl::reqbufs(&fd, QueueType::VideoCaptureMplane, MemoryType::Mmap, N_BUFFERS as _)?;
        let capture_mappings = (0..N_BUFFERS)
            .map(|i| -> Result<_> {
                let buf: ioctl::QueryBuffer = ioctl::querybuf(&fd, QueueType::VideoCaptureMplane, i)?;
                Ok(ioctl::mmap(&fd, buf.planes[0].mem_offset, buf.planes[0].length)?)
            })
            .collect::<Result<Vec<_>>>()?;

        ioctl::streamon(&fd, QueueType::VideoOutputMplane)?;
        ioctl::streamon(&fd, QueueType::VideoCaptureMplane)?;

        Ok(Self {
            config,
            fd,
            output_mappings,
            output_format,
            capture_mappings,
            pending_frames: VecDeque::new(),
            available_output_buffers: (0..N_BUFFERS).collect(),
            available_capture_buffers: (0..N_BUFFERS).collect(),
        })
    }

    // Blocks until output is available if `flushing` is `true` or if all our buffers are full.
    fn wait_for_output(&mut self, flushing: bool) -> Result<Option<VideoEncoderOutput<F>>> {
        if !flushing && !self.available_capture_buffers.is_empty() {
            // we want to saturate our buffers before blocking on frame output
            // TODO: we could potentially make a non-blocking call here to see if any output
            // happens to be ready
            return Ok(None);
        }

        Ok(match self.pending_frames.pop_front() {
            Some(f) => {
                // dequeue an output buffer
                let out_dqbuf: ioctl::V4l2Buffer = ioctl::dqbuf(&self.fd, QueueType::VideoOutputMplane)?;
                self.available_output_buffers.push(out_dqbuf.index() as _);

                // dequeue a capture buffer
                let cap_dqbuf: ioctl::V4l2Buffer = ioctl::dqbuf(&self.fd, QueueType::VideoCaptureMplane)?;
                let flags = cap_dqbuf.flags();
                let bytes = cap_dqbuf.get_first_plane().bytesused() as usize;
                let idx = cap_dqbuf.index() as usize;
                self.available_capture_buffers.push(idx);
                let data = self.capture_mappings[idx].data[..bytes].to_vec();
                Some(VideoEncoderOutput {
                    raw_frame: f,
                    encoded_frame: Some(EncodedVideoFrame {
                        data,
                        is_keyframe: flags.contains(ioctl::BufferFlags::KEYFRAME),
                    }),
                })
            }
            None => None,
        })
    }
}

impl<F: RawVideoFrame<u8>> VideoEncoder for V4L2Encoder<F> {
    type Error = V4L2EncoderError;
    type RawVideoFrame = F;

    fn encode(&mut self, input: F, frame_type: EncodedFrameType) -> Result<Option<VideoEncoderOutput<F>>> {
        // if we have full buffers, wait for a frame to finish
        let output = self.wait_for_output(false)?;

        // queue up the capture buffer
        {
            let capture_buffer_index = self
                .available_capture_buffers
                .pop()
                .expect("wait_for_output will block until we have an available buffer");

            let cap_qbuf = ioctl::QBuffer::<MmapHandle> {
                planes: vec![ioctl::QBufPlane::new(0)],
                ..Default::default()
            };
            ioctl::qbuf::<_, ()>(&self.fd, QueueType::VideoCaptureMplane, capture_buffer_index, cap_qbuf)?;
        }

        // queue up the raw frame in an output buffer
        {
            let output_buffer_index = self
                .available_output_buffers
                .pop()
                .expect("wait_for_output will block until we have an available buffer");

            if frame_type == EncodedFrameType::Key {
                ioctl::s_ctrl(&self.fd, bindings::V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1)?;
            }

            let mapping = &mut self.output_mappings[output_buffer_index];
            let mut offset = 0;
            for i in 0..self.config.input_format.planes() {
                let samples = input.samples(i);
                if samples.len() > mapping.len() - offset {
                    return Err(V4L2EncoderError::UnexpectedPlaneLayout);
                }
                mapping[offset..offset + samples.len()].copy_from_slice(samples);
                offset += samples.len();
            }
            if offset != self.output_format.plane_fmt[0].sizeimage as usize {
                return Err(V4L2EncoderError::UnexpectedPlaneLayout);
            }

            let out_qbuf = ioctl::QBuffer::<MmapHandle> {
                planes: vec![ioctl::QBufPlane::new(offset)],
                ..Default::default()
            };
            ioctl::qbuf::<_, ()>(&self.fd, QueueType::VideoOutputMplane, output_buffer_index, out_qbuf)?;

            self.pending_frames.push_back(input);
        }

        Ok(output)
    }

    fn flush(&mut self) -> Result<Option<VideoEncoderOutput<F>>> {
        self.wait_for_output(true)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    struct TestFrame {
        samples: Vec<Vec<u8>>,
    }

    impl RawVideoFrame<u8> for TestFrame {
        fn samples(&self, plane: usize) -> &[u8] {
            &self.samples[plane]
        }
    }

    #[test]
    fn test_video_encoder() {
        let mut encoder = V4L2Encoder::new(V4L2EncoderConfig {
            width: 1920,
            height: 1080,
            fps: 30.0,
            bitrate: Some(50000),
            keyframe_interval: Some(120),
            input_format: V4L2EncoderInputFormat::Yuv420Planar,
        })
        .unwrap();

        let mut encoded = vec![];
        let mut encoded_frames = 0;
        let mut encoded_keyframes = 0;

        let u = vec![200u8; 1920 * 1080 / 4];
        let v = vec![128u8; 1920 * 1080 / 4];
        for i in 0..90 {
            let mut y = Vec::with_capacity(1920 * 1080);
            for line in 0..1080 {
                let sample = if line / 12 == i {
                    // add some motion by drawing a line that moves from top to bottom
                    16
                } else {
                    (16.0 + (line as f64 / 1080.0) * 219.0).round() as u8
                };
                y.resize(y.len() + 1920, sample);
            }
            let frame = TestFrame {
                samples: vec![y, u.clone(), v.clone()],
            };
            if let Some(output) = encoder
                .encode(frame, if i % 30 == 0 { EncodedFrameType::Key } else { EncodedFrameType::Auto })
                .unwrap()
            {
                let mut encoded_frame = output.encoded_frame.expect("frame was not dropped");
                encoded.append(&mut encoded_frame.data);
                encoded_frames += 1;
                if encoded_frame.is_keyframe {
                    encoded_keyframes += 1;
                }
            }
        }
        while let Some(output) = encoder.flush().unwrap() {
            let mut encoded_frame = output.encoded_frame.expect("frame was not dropped");
            encoded.append(&mut encoded_frame.data);
            encoded_frames += 1;
            if encoded_frame.is_keyframe {
                encoded_keyframes += 1;
            }
        }

        assert_eq!(encoded_frames, 90);
        assert!(encoded.len() > 5000);
        assert!(encoded.len() < 30000);

        assert_eq!(encoded_keyframes, 3);

        // To inspect the output, uncomment these lines:
        //use std::io::Write;
        //std::fs::File::create("tmp.h264").unwrap().write_all(&encoded).unwrap();
    }
}
