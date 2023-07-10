use super::{analyzer, Analyzer, SegmentStorage, StreamInfo};
use crate::VideoMetadata;
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
}

impl<S: SegmentStorage> Segmenter<S> {
    pub fn new(config: SegmenterConfig, storage: S) -> Self {
        Self {
            config,
            storage,
            current_segment: None,
            analyzer: Analyzer::new(),
            pcr: None,
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

            if should_start_new_segment {
                if let Some(curr) = self.current_segment.take() {
                    let info = SegmentInfo::compile(&curr, self.analyzer.streams());
                    self.storage.finalize_segment(curr.segment, info).await?;
                }

                self.current_segment = Some(CurrentSegment {
                    segment: self.storage.new_segment().await?,
                    pcr: self.pcr.unwrap_or(0),
                    pts: None,
                    bytes_written: 0,
                    temi_timeline_descriptor: None,
                });

                self.analyzer.reset_streams_data();
            }

            if let Some(segment) = &mut self.current_segment {
                let mut pes_packet_header: Option<pes::PacketHeader> = None;
                // set the segment's pts if necessary
                if segment.pts.is_none() && self.analyzer.is_pes(p.packet_id) && p.payload_unit_start_indicator {
                    if let Some(payload) = p.payload.as_ref() {
                        pes_packet_header = Some(pes::PacketHeader::decode(payload)?.0);
                        segment.pts = pes_packet_header.as_ref().and_then(|h| h.optional_header.as_ref()).and_then(|h| h.pts);
                    }
                }

                if let Some(af) = p.adaptation_field {
                    if segment.temi_timeline_descriptor.is_none() {
                        segment.temi_timeline_descriptor = af.temi_timeline_descriptors.into_iter().next();
                    }
                    if !af.private_data_bytes.is_empty() {
                        // error out if the private data in a MPEG-TS packet is not in the beginning of PES
                        if self.analyzer.is_pes(p.packet_id) && !p.payload_unit_start_indicator {
                            return Err(Error::Mpeg2Decode(mpeg2::DecodeError::new("private data is not in the beginning of PES.")));
                        }
                        if let Some(payload) = p.payload {
                            let header = pes_packet_header.unwrap_or(pes::PacketHeader::decode(&payload)?.0);
                            if let Some(pts) = header.optional_header.as_ref().and_then(|h| h.pts) {
                                match self.analyzer.stream(p.packet_id) {
                                    Some(analyzer::Stream::AVCVideo { video_metadata, .. }) | Some(analyzer::Stream::HEVCVideo { video_metadata, .. }) => {
                                        add_video_metadata(video_metadata, segment.pts, pts, af.private_data_bytes.into_owned());
                                    }
                                    _ => {}
                                }
                            }
                        }
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
            let mut info = SegmentInfo::compile(&segment, self.analyzer.streams());
            for s in info.streams.iter_mut() {
                if let StreamInfo::Video { frame_count, .. } = s {
                    *frame_count += 1;
                }
            }
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
    fn compile<S: AsyncWrite + Unpin>(segment: &CurrentSegment<S>, streams: Vec<StreamInfo>) -> Self {
        Self {
            size: segment.bytes_written,
            presentation_time: segment.pts.map(|pts| Duration::from_micros((pts * 300) / 27)),
            streams,
            temi_timeline_descriptor: segment.temi_timeline_descriptor.clone(),
        }
    }
}

fn add_video_metadata(metadata_list: &mut Vec<VideoMetadata>, segment_pts: Option<u64>, pts: u64, private_data: Vec<u8>) {
    const PTS_ROLLOVER_MOD: u64 = 1 << 33;
    let pts0 = segment_pts.unwrap_or(metadata_list.first().map(|m| m.pts).unwrap_or(pts));
    metadata_list.push(VideoMetadata {
        pts: (pts + PTS_ROLLOVER_MOD - pts0) % PTS_ROLLOVER_MOD,
        private_data,
    });
}

#[cfg(test)]
mod test {
    use super::{super::segmentstorage::*, *};
    use std::{fs::File, io::Read};

    fn get_segment_frame_counts(segments: &[(Vec<u8>, SegmentInfo)]) -> Vec<u64> {
        let mut frame_counts = Vec::new();
        for (_, info) in segments {
            for stream in &info.streams {
                if let StreamInfo::Video { frame_count, .. } = stream {
                    frame_counts.push(*frame_count);
                }
            }
        }
        frame_counts
    }

    fn get_segment_audio_sample_counts(segments: &[(Vec<u8>, SegmentInfo)]) -> Vec<u64> {
        let mut sample_counts = Vec::new();
        for (_, info) in segments {
            for stream in &info.streams {
                if let StreamInfo::Audio { sample_count, .. } = stream {
                    sample_counts.push(*sample_count);
                }
            }
        }
        sample_counts
    }

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
        let frame_counts = get_segment_frame_counts(segments);
        assert_eq!(&frame_counts, &[250, 250, 100]);
        let audio_sample_counts = get_segment_audio_sample_counts(&segments);
        assert_eq!(audio_sample_counts, vec![197632, 196608, 87040]);
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
        let frame_counts = get_segment_frame_counts(segments);
        assert_eq!(&frame_counts, &[30, 3]);
        let audio_sample_counts = get_segment_audio_sample_counts(&segments);
        assert_eq!(audio_sample_counts, vec![78848, 3072]);
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
        let frame_counts = get_segment_frame_counts(segments);
        assert_eq!(&frame_counts, &[30, 1]);
        let audio_sample_counts = get_segment_audio_sample_counts(&segments);
        assert_eq!(audio_sample_counts, vec![49152, 0]);
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
        let frame_counts = get_segment_frame_counts(segments);
        assert_eq!(
            &frame_counts,
            &[30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 25, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 14]
        );
        let audio_sample_counts = get_segment_audio_sample_counts(&segments);
        assert_eq!(
            audio_sample_counts,
            vec![
                39936, 41984, 43008, 44032, 43008, 44032, 45056, 43008, 43008, 43008, 43008, 43008, 43008, 5120, 39936, 41984, 43008, 44032, 43008, 44032,
                45056, 43008, 43008, 27648, 1024
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
        let frame_counts = get_segment_frame_counts(segments);
        assert_eq!(&frame_counts, &[47, 60, 45]);
        let audio_sample_counts = get_segment_audio_sample_counts(&segments);
        assert_eq!(audio_sample_counts, vec![75776, 96256, 71680]);
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
        let frame_counts = get_segment_frame_counts(segments);
        assert_eq!(&frame_counts, &[59, 50]);
        let audio_sample_counts = get_segment_audio_sample_counts(&segments);
        // h264-SEI.ts doesn't contain audio
        assert_eq!(audio_sample_counts, vec![]);
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
        let frame_counts = get_segment_frame_counts(segments);
        assert_eq!(&frame_counts, &[29, 29, 8]);
        let audio_sample_counts = get_segment_audio_sample_counts(&segments);
        assert_eq!(audio_sample_counts, vec![39936, 41984, 14336]);
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
                min_segment_duration: Duration::from_secs(1),
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
        let frame_counts = get_segment_frame_counts(segments);
        assert_eq!(&frame_counts, &[]);
        let audio_sample_counts = get_segment_audio_sample_counts(&segments);
        assert_eq!(audio_sample_counts, vec![144384, 144384, 144384, 144384, 144384, 144384, 10240]);
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
        let frame_counts = get_segment_frame_counts(segments);
        assert_eq!(&frame_counts, &[72, 72, 72]);
        let audio_sample_counts = get_segment_audio_sample_counts(&segments);
        assert_eq!(audio_sample_counts, vec![]);
    }
}
