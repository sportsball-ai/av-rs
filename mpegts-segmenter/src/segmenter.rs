use super::{analyzer, Analyzer, SegmentStorage, StreamInfo};
use mpeg2::ts::{Packet, PACKET_LENGTH};
use std::{fmt, io, time::Duration};
use tokio::prelude::*;

struct CurrentSegment<S: AsyncWrite + Unpin> {
    segment: S,
    pcr: u64,
    bytes_written: usize,
}

pub struct SegmenterConfig {
    /// The minimum duration for the segment. The next segment will begin at the next keyframe
    /// after this duration has elapsed.
    pub min_segment_duration: Duration,
}

#[derive(Debug)]
pub enum Error {
    IO(io::Error),
    Other(Box<dyn std::error::Error + Send + Sync>),
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::IO(e) => write!(f, "io error: {}", e),
            Self::Other(e) => e.fmt(f),
        }
    }
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Self::IO(e)
    }
}

impl From<Box<dyn std::error::Error + Send + Sync>> for Error {
    fn from(e: Box<dyn std::error::Error + Send + Sync>) -> Self {
        Self::Other(e)
    }
}

pub struct Segmenter<S: SegmentStorage> {
    config: SegmenterConfig,
    storage: S,
    current_segment: Option<CurrentSegment<S::Segment>>,
    analyzer: Analyzer,
    pcr: Option<u64>,
    streams_before_segment: Vec<StreamInfo>,
}

impl<S: SegmentStorage> Segmenter<S> {
    pub fn new(config: SegmenterConfig, storage: S) -> Self {
        Self {
            config,
            storage,
            current_segment: None,
            analyzer: Analyzer::new(),
            pcr: None,
            streams_before_segment: Vec::new(),
        }
    }

    pub fn storage(&self) -> &S {
        &self.storage
    }

    pub fn storage_mut(&mut self) -> &mut S {
        &mut self.storage
    }

    /// Writes packets to the segmenter. The segmenter does not do any internal buffering, so buf
    /// must be divisible by 188 (the MPEG TS packet length).
    pub async fn write(&mut self, buf: &[u8]) -> Result<(), Error> {
        if buf.len() % PACKET_LENGTH != 0 {
            return Err(io::Error::new(io::ErrorKind::Other, "write length not divisible by packet length").into());
        }

        for buf in buf.chunks(PACKET_LENGTH) {
            let p = Packet::decode(&buf)?;
            self.analyzer.handle_packet(&p)?;

            if let Some(af) = &p.adaptation_field {
                self.pcr = af.program_clock_reference_27mhz.or(self.pcr);
            }

            let mut should_start_new_segment = false;
            if self.analyzer.is_pes(p.packet_id) {
                if let Some(pcr) = self.pcr {
                    should_start_new_segment = match &self.current_segment {
                        Some(current_segment) => {
                            let elapsed_seconds = (pcr as i64 - current_segment.pcr as i64) as f64 / 27_000_000.0;
                            if (self.analyzer.is_video(p.packet_id) || !self.analyzer.has_video())
                                && (elapsed_seconds < -1.0 || elapsed_seconds >= self.config.min_segment_duration.as_secs_f64())
                            {
                                // start a new segment if this is a keyframe
                                if p.adaptation_field.map(|af| af.random_access_indicator).unwrap_or(false) {
                                    true
                                } else if let Some(payload) = p.payload {
                                    // some muxers don't set RAI bits. if possible, see if this
                                    // packet includes the start of a keyframe
                                    let mut is_keyframe = false;
                                    match self.analyzer.stream(p.packet_id) {
                                        Some(analyzer::Stream::AVCVideo { .. }) => {
                                            for nalu in h264::iterate_annex_b(&payload) {
                                                let nalu_type = nalu[0] & h264::NAL_UNIT_TYPE_MASK;
                                                is_keyframe |= nalu_type == h264::NAL_UNIT_TYPE_CODED_SLICE_OF_IDR_PICTURE;
                                            }
                                        }
                                        Some(analyzer::Stream::HEVCVideo { .. }) => {
                                            use h264::Decode;
                                            for nalu in h265::iterate_annex_b(&payload) {
                                                let mut bs = h265::Bitstream::new(nalu.iter().copied());
                                                let header = h265::NALUnitHeader::decode(&mut bs)?;
                                                if header.nuh_layer_id.0 == 0 {
                                                    match header.nal_unit_type.0 {
                                                        16..=21 => is_keyframe = true,
                                                        _ => {}
                                                    }
                                                }
                                            }
                                        }
                                        _ => {}
                                    }
                                    is_keyframe
                                } else {
                                    false
                                }
                            } else {
                                false
                            }
                        }
                        None => true,
                    }
                }
            }

            if should_start_new_segment {
                if let Some(prev) = self.current_segment.take() {
                    let info = SegmentInfo::compile(&prev, self.analyzer.streams(), &self.streams_before_segment);
                    self.storage.finalize_segment(prev.segment, info).await?;
                }

                self.streams_before_segment = self.analyzer.streams();

                self.current_segment = Some(CurrentSegment {
                    segment: self.storage.new_segment().await?,
                    pcr: self.pcr.unwrap_or(0),
                    bytes_written: 0,
                });
            }

            if let Some(segment) = &mut self.current_segment {
                segment.segment.write_all(&buf).await?;
                segment.bytes_written += buf.len();
            }
        }

        Ok(())
    }

    pub async fn flush(&mut self) -> Result<(), Error> {
        if let Some(segment) = self.current_segment.take() {
            let info = SegmentInfo::compile(&segment, self.analyzer.streams(), &self.streams_before_segment);
            self.storage.finalize_segment(segment.segment, info).await?;
        }
        Ok(())
    }
}

pub async fn segment<R: AsyncRead + Unpin, S: SegmentStorage>(mut r: R, config: SegmenterConfig, storage: S) -> Result<(), Error> {
    let mut segmenter = Segmenter::new(config, storage);

    // XXX: the buffer size should be at least 1316, which is the max SRT payload size
    let mut buf = [0u8; PACKET_LENGTH * 20];

    loop {
        let bytes_read = r.read(&mut buf).await?;
        if bytes_read == 0 {
            break;
        }
        segmenter.write(&buf[..bytes_read]).await?;
    }
    segmenter.flush().await?;

    Ok(())
}

#[derive(Debug, Clone)]
pub struct SegmentInfo {
    pub size: usize,
    pub streams: Vec<StreamInfo>,
}

impl SegmentInfo {
    fn compile<S: AsyncWrite + Unpin>(segment: &CurrentSegment<S>, streams: Vec<StreamInfo>, prev_streams: &[StreamInfo]) -> Self {
        Self {
            size: segment.bytes_written,
            streams: if streams.len() != prev_streams.len() {
                streams
            } else {
                streams
                    .iter()
                    .zip(prev_streams)
                    .filter_map(|(after, before)| match (before, after) {
                        (
                            StreamInfo::Audio {
                                sample_count: prev_sample_count,
                                ..
                            },
                            StreamInfo::Audio {
                                channel_count,
                                sample_rate,
                                sample_count,
                                rfc6381_codec,
                            },
                        ) => Some(StreamInfo::Audio {
                            channel_count: *channel_count,
                            sample_rate: *sample_rate,
                            sample_count: if sample_count >= prev_sample_count {
                                sample_count - prev_sample_count
                            } else {
                                0
                            },
                            rfc6381_codec: rfc6381_codec.clone(),
                        }),
                        (
                            StreamInfo::Video {
                                frame_count: prev_frame_count, ..
                            },
                            StreamInfo::Video {
                                width,
                                height,
                                frame_rate,
                                frame_count,
                                rfc6381_codec,
                                timing,
                            },
                        ) => Some(StreamInfo::Video {
                            width: *width,
                            height: *height,
                            frame_rate: *frame_rate,
                            frame_count: if frame_count >= prev_frame_count { frame_count - prev_frame_count } else { 0 },
                            rfc6381_codec: rfc6381_codec.clone(),
                            timing: timing.clone(),
                        }),
                        (StreamInfo::Other, StreamInfo::Other) => Some(StreamInfo::Other),
                        _ => None,
                    })
                    .collect()
            },
        }
    }
}

#[cfg(test)]
mod test {
    use super::{super::segmentstorage::*, *};
    use std::{fs::File, io::Read};

    #[tokio::test]
    async fn test_segmenter() {
        let mut storage = MemorySegmentStorage::new();

        {
            let mut f = File::open("src/testdata/h264.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            segment(
                buf.as_slice(),
                SegmenterConfig {
                    min_segment_duration: Duration::from_secs(3),
                },
                &mut storage,
            )
            .await
            .unwrap();
        }

        let segments = storage.segments();
        assert_eq!(segments.len(), 4);
        for (_, info) in segments {
            assert_eq!(info.streams.len(), 2);
            for stream in &info.streams {
                match stream {
                    StreamInfo::Audio {
                        channel_count,
                        sample_count,
                        sample_rate,
                        ..
                    } => {
                        assert_eq!(*channel_count, 2);
                        assert_eq!(*sample_count > 0, true);
                        assert_eq!(*sample_rate, 48000);
                    }
                    StreamInfo::Video {
                        frame_rate,
                        width,
                        height,
                        frame_count,
                        ..
                    } => {
                        assert_eq!(*frame_count > 0, true);
                        assert_eq!(*frame_rate, 59.94);
                        assert_eq!(*width, 1280);
                        assert_eq!(*height, 720);
                    }
                    _ => panic!("unexpected stream type"),
                }
            }
        }
    }

    #[tokio::test]
    async fn test_segmenter_8k() {
        let mut storage = MemorySegmentStorage::new();

        {
            let mut f = File::open("src/testdata/h264-8k.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            segment(
                buf.as_slice(),
                SegmenterConfig {
                    min_segment_duration: Duration::from_secs(1),
                },
                &mut storage,
            )
            .await
            .unwrap();
        }

        let segments = storage.segments();
        assert_eq!(segments.len(), 2);
    }

    #[tokio::test]
    async fn test_segmenter_h265_8k() {
        let mut storage = MemorySegmentStorage::new();

        {
            let mut f = File::open("src/testdata/h265-8k.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            segment(
                buf.as_slice(),
                SegmenterConfig {
                    min_segment_duration: Duration::from_secs(1),
                },
                &mut storage,
            )
            .await
            .unwrap();
        }

        let segments = storage.segments();
        assert_eq!(segments.len(), 2);
    }

    #[tokio::test]
    /// The file used for this test was generated by an ffmpeg command that was restarted half-way
    /// through. This causes the PCR to reset, which at one point caused a panic due to overflow.
    async fn test_segmenter_restart() {
        let mut storage = MemorySegmentStorage::new();

        {
            let mut f = File::open("src/testdata/restart.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            segment(
                buf.as_slice(),
                SegmenterConfig {
                    min_segment_duration: Duration::from_secs(1),
                },
                &mut storage,
            )
            .await
            .unwrap();
        }

        let segments = storage.segments();
        assert_eq!(segments.len(), 25);
    }

    #[tokio::test]
    /// The file used for this test has PCR fields only for non-pes packets. It is also interlaced
    /// and has keyframes beginning on packets without adaptation fields.
    async fn test_segmenter_program() {
        let mut storage = MemorySegmentStorage::new();

        {
            let mut f = File::open("src/testdata/program.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            segment(
                buf.as_slice(),
                SegmenterConfig {
                    min_segment_duration: Duration::from_secs(1),
                },
                &mut storage,
            )
            .await
            .unwrap();
        }

        let segments = storage.segments();
        assert_eq!(segments.len(), 3);
    }
}
