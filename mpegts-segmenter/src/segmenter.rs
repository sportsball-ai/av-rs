use super::{analyzer, Analyzer, SegmentStorage, StreamInfo};
use crate::VideoMetadata;
use mpeg2::{
    pes,
    temi::TEMITimelineDescriptor,
    ts::{Packet, PACKET_LENGTH},
};
use std::{fmt, io, time::Duration};
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

pub const PTS_ROLLOVER_MOD: u64 = 1 << 33;
pub const VIDEO_PTS_BASE: u64 = 90_000;

struct CurrentSegment<S: AsyncWrite + Unpin> {
    segment: S,
    pcr: u64,
    pts: Option<u64>,
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
    previous_segment: Option<CurrentSegment<S::Segment>>,
    previous_segment_info: Option<SegmentInfo>,
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
            previous_segment: None,
            previous_segment_info: None,
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
                                    // Some muxers don't set RAI bits. If possible, see if this
                                    // packet includes the start of a keyframe. This is only the
                                    // first packet (<188 bytes), not the entire frame. That means
                                    // we need to be defensive and not error out if we reach the
                                    // end unexpectedly soon.
                                    let mut is_keyframe = false;
                                    match self.analyzer.stream(p.packet_id) {
                                        Some(analyzer::Stream::AVCVideo { .. }) => {
                                            for nalu in h264::iterate_annex_b(&payload) {
                                                if !nalu.is_empty() {
                                                    let nalu_type = nalu[0] & h264::NAL_UNIT_TYPE_MASK;
                                                    is_keyframe |= nalu_type == h264::NAL_UNIT_TYPE_CODED_SLICE_OF_IDR_PICTURE;
                                                }
                                            }
                                        }
                                        Some(analyzer::Stream::HEVCVideo { .. }) => {
                                            use h265::Decode;
                                            for nalu in h265::iterate_annex_b(&payload) {
                                                let mut bs = h265::Bitstream::new(nalu.iter().copied());
                                                if let Ok(header) = h265::NALUnitHeader::decode(&mut bs) {
                                                    if header.nuh_layer_id.0 == 0 {
                                                        if let 16..=21 = header.nal_unit_type.0 {
                                                            is_keyframe = true
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        Some(analyzer::Stream::ADTSAudio { .. }) => {
                                            // note this is only reachable if this is an audio-only stream
                                            is_keyframe = elapsed_seconds < -1.0 || elapsed_seconds >= self.config.min_segment_duration.as_secs_f64().max(3.0);
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

            let mut pes_packet_header = None;
            let first_temi_timeline_descriptor;
            let (mut packet_id, mut video_metadata) = (0, None);
            if let Some(af) = p.adaptation_field {
                first_temi_timeline_descriptor = af.temi_timeline_descriptors.into_iter().next();
                if !af.private_data_bytes.is_empty() {
                    // error out if the private data in a MPEG-TS packet is not in the beginning of PES
                    if self.analyzer.is_pes(p.packet_id) && !p.payload_unit_start_indicator {
                        return Err(Error::Mpeg2Decode(mpeg2::DecodeError::new("private data is not in the beginning of PES.")));
                    }
                    if let Some(payload) = p.payload.as_ref() {
                        let (header, _) = pes::PacketHeader::decode(payload)?;
                        if let Some(pts) = header.optional_header.as_ref().and_then(|h| h.pts) {
                            match self.analyzer.stream(p.packet_id) {
                                Some(analyzer::Stream::AVCVideo { .. }) | Some(analyzer::Stream::HEVCVideo { .. }) => {
                                    packet_id = p.packet_id;
                                    video_metadata = Some(VideoMetadata {
                                        pts,
                                        private_data: af.private_data_bytes.into_owned(),
                                    })
                                }
                                _ => {}
                            }
                        }
                        pes_packet_header = Some(header);
                    }
                }
            } else {
                first_temi_timeline_descriptor = None;
            }

            if should_start_new_segment {
                if let Some(curr) = self.current_segment.take() {
                    self.previous_segment_info = Some(SegmentInfo::compile(&curr, self.analyzer.streams(), &self.streams_before_segment));
                    self.previous_segment = Some(curr);
                }

                self.streams_before_segment = self.analyzer.streams();

                self.current_segment = Some(CurrentSegment {
                    segment: self.storage.new_segment().await?,
                    pcr: self.pcr.unwrap_or(0),
                    pts: None,
                    bytes_written: 0,
                    temi_timeline_descriptor: None,
                });

                self.analyzer.reset_stream_metadata();
                self.analyzer.reset_timecodes();
            }

            if let Some(video_metadata) = video_metadata {
                self.analyzer.add_stream_metadata(packet_id, video_metadata);
            }

            if let Some(segment) = &mut self.current_segment {
                // set the segment's pts if necessary
                if segment.pts.is_none() && self.analyzer.is_pes(p.packet_id) && p.payload_unit_start_indicator {
                    if pes_packet_header.is_none() {
                        pes_packet_header = if let Some(payload) = p.payload {
                            let (header, _) = pes::PacketHeader::decode(&payload)?;
                            Some(header)
                        } else {
                            None
                        }
                    }
                    segment.pts = pes_packet_header.and_then(|h| h.optional_header).and_then(|h| h.pts);
                    if let Some((previous_segment, mut previous_segment_info)) = self.previous_segment.take().zip(self.previous_segment_info.take()) {
                        if let Some((prev_pts, curr_pts)) = previous_segment.pts.zip(segment.pts) {
                            let duration = (curr_pts + PTS_ROLLOVER_MOD - prev_pts) % PTS_ROLLOVER_MOD;
                            // set the segment duration only if it's within a reasonable value.
                            if duration < (self.config.min_segment_duration.as_secs_f64() * VIDEO_PTS_BASE as f64 * 3.0) as u64 {
                                previous_segment_info.duration = Some(duration);
                            } else {
                                return Err(Error::Other(
                                    "The segment duration is outside the normal range, which could be due to an invalid presentation time.".into(),
                                ));
                            }
                        }
                        self.storage.finalize_segment(previous_segment.segment, previous_segment_info).await?;
                    }
                }

                if segment.temi_timeline_descriptor.is_none() {
                    segment.temi_timeline_descriptor = first_temi_timeline_descriptor;
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

fn convert_to_relative_pts(video_metadata: &[VideoMetadata], first_pts: Option<u64>) -> Vec<VideoMetadata> {
    if video_metadata.is_empty() {
        vec![]
    } else {
        let pts0 = first_pts.unwrap_or(video_metadata[0].pts);
        video_metadata
            .iter()
            .map(|m| VideoMetadata {
                pts: (m.pts + PTS_ROLLOVER_MOD - pts0) % PTS_ROLLOVER_MOD,
                private_data: m.private_data.clone(),
            })
            .collect()
    }
}

#[derive(Debug, Clone)]
pub struct SegmentInfo {
    pub size: usize,
    pub presentation_time: Option<Duration>,
    pub streams: Vec<StreamInfo>,
    pub temi_timeline_descriptor: Option<TEMITimelineDescriptor>,
    // in presentation time units of 90_000.
    pub duration: Option<u64>,
}

impl SegmentInfo {
    fn compile<S: AsyncWrite + Unpin>(segment: &CurrentSegment<S>, streams: Vec<StreamInfo>, prev_streams: &[StreamInfo]) -> Self {
        Self {
            size: segment.bytes_written,
            presentation_time: segment.pts.map(|pts| Duration::from_micros((pts * 300) / 27)),
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
                                video_metadata,
                                ..
                            },
                        ) => Some(StreamInfo::Video {
                            width: *width,
                            height: *height,
                            frame_rate: *frame_rate,
                            frame_count: if frame_count >= prev_frame_count { frame_count - prev_frame_count } else { 0 },
                            rfc6381_codec: rfc6381_codec.clone(),
                            timecode: timecode.clone(),
                            is_interlaced: *is_interlaced,
                            video_metadata: convert_to_relative_pts(video_metadata, segment.pts),
                        }),
                        (StreamInfo::Other, StreamInfo::Other) => Some(StreamInfo::Other),
                        _ => None,
                    })
                    .collect()
            },
            temi_timeline_descriptor: segment.temi_timeline_descriptor.clone(),
            duration: None,
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
        let durations = segments.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(durations, [375375, 375375]);
        assert_eq!(segments.len(), durations.len() + 1);
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
        let durations = segments.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(durations, [90068]);
        assert_eq!(segments.len(), durations.len() + 1);
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
        let durations = segments.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(durations, [90103]);
        assert_eq!(segments.len(), durations.len() + 1);
    }

    #[tokio::test]
    /// The file used for this test was generated by an ffmpeg command that was restarted half-way
    /// through. This causes the PCR to reset, which at one point caused a panic due to overflow.
    async fn test_segmenter_restart() {
        let mut storage1 = MemorySegmentStorage::new();
        let mut storage2 = MemorySegmentStorage::new();

        {
            let mut f = File::open("src/testdata/restart.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            let result = segment(
                buf.as_slice(),
                SegmenterConfig {
                    min_segment_duration: Duration::from_secs(1),
                },
                &mut storage1,
            )
            .await;
            assert_eq!(
                result.err().unwrap().to_string(),
                "The segment duration is outside the normal range, which could be due to an invalid presentation time."
            );

            segment(
                &buf[1466400..],
                SegmenterConfig {
                    min_segment_duration: Duration::from_secs(1),
                },
                &mut storage2,
            )
            .await
            .unwrap();
        }

        let segments1 = storage1.segments();
        assert_eq!(segments1.len(), 13);
        assert_eq!(
            segments1.iter().map(|(_, s)| s.presentation_time.unwrap().as_secs_f64()).collect::<Vec<_>>(),
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
            ]
        );
        let durations = segments1.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(
            durations,
            [90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000]
        );

        let segments2 = storage2.segments();
        assert_eq!(segments2.len(), 11);
        assert_eq!(
            segments2.iter().map(|(_, s)| s.presentation_time.unwrap().as_secs_f64()).collect::<Vec<_>>(),
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
            ]
        );

        let durations = segments2.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(durations, [90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000, 90000]);
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
        let durations = segments.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(durations, [142643, 180180]);
        assert_eq!(segments.len(), durations.len() + 1);
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
        let durations = segments.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(durations, [180000]);
        assert_eq!(segments.len(), durations.len() + 1);
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
        // Sine there are missing pts in PES optional header in the synthetic test video clip,
        // we will be unable to infer the durations of the segments in this case.
        let durations = segments.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(durations, []);
    }

    // This is a regression test to ensure that if the first packet of a keyframe ends with the
    // H264 start sequence ([0, 0, 0, 1]), we don't panic.
    #[tokio::test]
    async fn test_segmenter_empty_nalu() {
        let mut storage = MemorySegmentStorage::new();

        let mut f = File::open("src/testdata/h264.ts").unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();

        // Cut off the data right after the packet we're going to alter.
        let last_video_frame_start_packet = 47098;
        buf.resize((last_video_frame_start_packet + 1) * PACKET_LENGTH, 0);

        // Then replace the last 4 bytes with the H264 start sequence.
        let first_video_packet = &mut buf[last_video_frame_start_packet * PACKET_LENGTH..(last_video_frame_start_packet + 1) * PACKET_LENGTH];
        first_video_packet[PACKET_LENGTH - 4..].copy_from_slice(&[0, 0, 0, 1]);

        // Just make sure we don't panic.
        segment(
            buf.as_ref(),
            SegmenterConfig {
                min_segment_duration: Duration::from_secs(2),
            },
            &mut storage,
        )
        .await
        .unwrap();
    }

    #[tokio::test]
    async fn test_segmenter_audio_only() {
        let mut storage = MemorySegmentStorage::new();
        {
            let mut f = File::open("src/testdata/audio-only.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            segment(
                buf.as_slice(),
                SegmenterConfig {
                    min_segment_duration: Duration::from_millis(2500),
                },
                &mut storage,
            )
            .await
            .unwrap();
        }

        let segments = storage.segments();
        assert!(segments
            .iter()
            .all(|(_, s)| s.streams.len() == 1 && matches!(s.streams[0], StreamInfo::Audio { .. })));

        assert_eq!(segments.len(), 7);
        assert_eq!(
            segments.iter().map(|(_, s)| s.presentation_time.unwrap().as_secs_f64()).collect::<Vec<_>>(),
            vec![77774.269144, 77777.277144, 77780.285144, 77783.293144, 77786.301144, 77789.309144, 77792.317144]
        );
        let durations = segments.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(durations, [270720, 270720, 270720, 270720, 270720, 270720]);
        assert_eq!(segments.len(), durations.len() + 1);
    }

    #[tokio::test]
    async fn test_segmenter_video_cropping() {
        let mut storage = MemorySegmentStorage::new();
        {
            let mut f = File::open("src/testdata/with-cropping.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            segment(
                buf.as_slice(),
                SegmenterConfig {
                    min_segment_duration: Duration::from_millis(2900),
                },
                &mut storage,
            )
            .await
            .unwrap();
        }

        let segments = storage.segments();
        assert_eq!(segments.len(), 3);
        assert!(segments
            .iter()
            .all(|(_, segment_info)| segment_info.streams.len() == 1 && matches!(segment_info.streams[0], StreamInfo::Video { .. })));

        for segment in segments {
            match &segment.1.streams[0] {
                StreamInfo::Video { video_metadata, .. } => {
                    assert_eq!(video_metadata.len(), 72);
                    assert!(video_metadata
                        .iter()
                        .all(|data| data.private_data.len() == 80 && data.private_data[..4] == [b't', b'x', b'm', b'0']));

                    let ptses = video_metadata.iter().map(|data| data.pts).collect::<Vec<u64>>();
                    assert_eq!(ptses[0], 0);
                    for i in 1..ptses.len() {
                        assert!(ptses[i] > ptses[i - 1]);
                    }
                }
                _ => unreachable!(),
            }
        }
        let durations = segments.iter().flat_map(|(_, info)| info.duration).collect::<Vec<u64>>();
        assert_eq!(durations, [267001, 271500]);
        assert_eq!(segments.len(), durations.len() + 1);
    }
}
