use super::{analyzer, Analyzer, SegmentStorage, StreamInfo};
use mpeg2::{
    pes,
    temi::TEMITimelineDescriptor,
    ts::{Packet, PACKET_LENGTH},
};
use std::{fmt, io, time::Duration};
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

struct CurrentSegment<S: AsyncWrite + Unpin> {
    segment: S,
    pcr: u64,
    pts: Option<Duration>,
    bytes_written: usize,
    temi_timeline_descriptor: Option<TEMITimelineDescriptor>,
}

pub struct SegmenterConfig {
    /// The minimum duration for the segment. The next segment will begin at the next keyframe
    /// after this duration has elapsed.
    pub min_segment_duration: Duration,
}

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    Mpeg2Decode(mpeg2::DecodeError),
    Other(Box<dyn std::error::Error + Send + Sync>),
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(e) => write!(f, "io error: {}", e),
            Self::Mpeg2Decode(e) => write!(f, "mpeg2 decode error: {}", e),
            Self::Other(e) => e.fmt(f),
        }
    }
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

impl From<mpeg2::DecodeError> for Error {
    fn from(e: mpeg2::DecodeError) -> Self {
        Self::Mpeg2Decode(e)
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
            let p = Packet::decode(buf)?;
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
                                if p.adaptation_field.as_ref().and_then(|af| af.random_access_indicator).unwrap_or(false) {
                                    true
                                } else if let Some(payload) = &p.payload {
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
                                                    if let 16..=21 = header.nal_unit_type.0 {
                                                        is_keyframe = true
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
                    pts: None,
                    bytes_written: 0,
                    temi_timeline_descriptor: None,
                });

                self.analyzer.reset_timecodes();
            }

            if let Some(segment) = &mut self.current_segment {
                // set the segment's pts if necessary
                if segment.pts.is_none() && self.analyzer.is_pes(p.packet_id) && p.payload_unit_start_indicator {
                    if let Some(payload) = p.payload {
                        let (header, _) = pes::PacketHeader::decode(&payload)?;
                        segment.pts = header.optional_header.and_then(|h| h.pts).map(|pts| Duration::from_micros((pts * 300) / 27));
                    }
                }

                if segment.temi_timeline_descriptor.is_none() {
                    if let Some(af) = p.adaptation_field {
                        segment.temi_timeline_descriptor = af.temi_timeline_descriptors.into_iter().next();
                    }
                }

                segment.segment.write_all(buf).await?;
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
    pub presentation_time: Option<Duration>,
    pub streams: Vec<StreamInfo>,
    pub temi_timeline_descriptor: Option<TEMITimelineDescriptor>,
}

impl SegmentInfo {
    fn compile<S: AsyncWrite + Unpin>(segment: &CurrentSegment<S>, streams: Vec<StreamInfo>, prev_streams: &[StreamInfo]) -> Self {
        Self {
            size: segment.bytes_written,
            presentation_time: segment.pts,
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
                                timecode,
                                is_interlaced,
                            },
                        ) => Some(StreamInfo::Video {
                            width: *width,
                            height: *height,
                            frame_rate: *frame_rate,
                            frame_count: if frame_count >= prev_frame_count { frame_count - prev_frame_count } else { 0 },
                            rfc6381_codec: rfc6381_codec.clone(),
                            timecode: timecode.clone(),
                            is_interlaced: *is_interlaced,
                        }),
                        (StreamInfo::Other, StreamInfo::Other) => Some(StreamInfo::Other),
                        _ => None,
                    })
                    .collect()
            },
            temi_timeline_descriptor: segment.temi_timeline_descriptor.clone(),
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
        assert_eq!(segments.len(), 3);
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
                        assert!(*sample_count > 0);
                        assert_eq!(*sample_rate, 48000);
                    }
                    StreamInfo::Video {
                        frame_rate,
                        width,
                        height,
                        frame_count,
                        ..
                    } => {
                        assert!(*frame_count > 0);
                        assert!((*frame_rate - 59.94).abs() < std::f64::EPSILON);
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
        assert_eq!(
            segments.iter().map(|(_, s)| s.presentation_time.unwrap().as_secs_f64()).collect::<Vec<_>>(),
            vec![924.279_588, 925.280_344]
        );
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
        assert_eq!(
            segments.iter().map(|(_, s)| s.presentation_time.unwrap().as_secs_f64()).collect::<Vec<_>>(),
            vec![731.629_855, 732.631]
        );
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
        assert_eq!(
            segments.iter().map(|(_, s)| s.presentation_time.unwrap().as_secs_f64()).collect::<Vec<_>>(),
            vec![
                1.423_222,
                2.423_222,
                3.423_222,
                4.423_222,
                5.423_222,
                6.423_222,
                7.423_222,
                8.423_221_999_999_999,
                9.423_221_999_999_999,
                10.423_221_999_999_999,
                11.423_221_999_999_999,
                12.423_221_999_999_999,
                13.423_221_999_999_999,
                14.423_221_999_999_999,
                1.423_222,
                2.423_222,
                3.423_222,
                4.423_222,
                5.423_222,
                6.423_222,
                7.423_222,
                8.423_221_999_999_999,
                9.423_221_999_999_999,
                10.423_221_999_999_999,
                11.423_221_999_999_999,
            ]
        );
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
        let timecodes = segments
            .iter()
            .flat_map(|(_, info)| {
                info.streams.iter().filter_map(|stream| match stream {
                    StreamInfo::Video { timecode, .. } => timecode.clone(),
                    _ => None,
                })
            })
            .collect::<Vec<analyzer::Timecode>>();

        assert_eq!(segments.len(), 3);
        assert_eq!(
            segments.iter().map(|(_, s)| s.presentation_time.unwrap().as_secs_f64()).collect::<Vec<_>>(),
            vec![8_077.017_166, 8_078.602_088, 8_080.604_088]
        );
        assert_eq!(
            timecodes,
            vec![
                analyzer::Timecode {
                    hours: 18,
                    minutes: 57,
                    seconds: 26,
                    frames: 2
                },
                analyzer::Timecode {
                    hours: 18,
                    minutes: 57,
                    seconds: 27,
                    frames: 2
                },
                analyzer::Timecode {
                    hours: 18,
                    minutes: 57,
                    seconds: 29,
                    frames: 2
                }
            ]
        );
    }

    #[tokio::test]
    /// The file used for this test has SEI NALU's with partial PicTimings.
    async fn test_segmenter_program_with_partial_timecodes() {
        let mut storage = MemorySegmentStorage::new();

        {
            let mut f = File::open("src/testdata/h264-SEI.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            segment(
                buf.as_slice(),
                SegmenterConfig {
                    min_segment_duration: Duration::from_secs(2),
                },
                &mut storage,
            )
            .await
            .unwrap();
        }

        let segments = storage.segments();

        let timecodes = segments
            .iter()
            .flat_map(|(_, info)| {
                info.streams.iter().filter_map(|stream| match stream {
                    StreamInfo::Video { timecode, .. } => timecode.clone(),
                    _ => None,
                })
            })
            .collect::<Vec<analyzer::Timecode>>();

        assert_eq!(segments.len(), 2);
        assert_eq!(
            segments.iter().map(|(_, s)| s.presentation_time.unwrap().as_secs_f64()).collect::<Vec<_>>(),
            vec![2.4, 4.4]
        );
        assert_eq!(
            timecodes,
            vec![
                analyzer::Timecode {
                    hours: 0,
                    minutes: 0,
                    seconds: 5,
                    frames: 24
                },
                analyzer::Timecode {
                    hours: 0,
                    minutes: 0,
                    seconds: 7,
                    frames: 22
                }
            ]
        );
    }

    #[tokio::test]
    async fn test_segmenter_with_temi_timeline_descriptor() {
        let mut storage = MemorySegmentStorage::new();
        {
            let mut f = File::open("src/testdata/temi-timeline-ntp-ts.ts").unwrap();
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
        let ntp_timestamps: Vec<Option<u64>> = segments
            .iter()
            .map(|s| s.1.temi_timeline_descriptor.as_ref().and_then(|temi| temi.ntp_timestamp))
            .collect();
        assert_eq!(
            ntp_timestamps,
            &[Some(16592063487166754097), Some(16592063487167157824), Some(16592063487167574436)]
        );
    }
}
