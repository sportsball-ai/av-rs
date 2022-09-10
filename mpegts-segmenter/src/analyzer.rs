use mpeg2::{pes, ts};
use mpeg4::AudioDataTransportStream;

use std::{collections::VecDeque, error::Error};

pub type Result<T> = std::result::Result<T, Box<dyn Error + Send + Sync>>;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Timecode {
    pub hours: u8,
    pub minutes: u8,
    pub seconds: u8,
    pub frames: u8,
}

#[allow(clippy::large_enum_variant)]
#[derive(Clone)]
pub enum Stream {
    ADTSAudio {
        pes: pes::Stream,
        channel_count: u32,
        sample_rate: u32,
        sample_count: u64,
        rfc6381_codec: Option<String>,
        object_type_indication: u8,
    },
    AVCVideo {
        pes: pes::Stream,
        width: u32,
        height: u32,
        frame_rate: f64,
        rfc6381_codec: Option<String>,
        is_interlaced: bool,
        access_unit_counter: h264::AccessUnitCounter,
        timecode: Option<Timecode>,
        last_timecode: Option<Timecode>,
        last_vui_parameters: Option<h264::VUIParameters>,
        pts_analyzer: PTSAnalyzer,
    },
    HEVCVideo {
        pes: pes::Stream,
        width: u32,
        height: u32,
        frame_rate: f64,
        rfc6381_codec: Option<String>,
        access_unit_counter: h265::AccessUnitCounter,
        pts_analyzer: PTSAnalyzer,
    },
    Other(u8),
}

impl Stream {
    pub fn is_video(&self) -> bool {
        matches!(*self, Stream::AVCVideo { .. } | Stream::HEVCVideo { .. })
    }

    pub fn info(&self) -> StreamInfo {
        match self {
            Self::ADTSAudio {
                channel_count,
                sample_rate,
                sample_count,
                rfc6381_codec,
                ..
            } => StreamInfo::Audio {
                channel_count: *channel_count,
                sample_rate: *sample_rate,
                sample_count: *sample_count,
                rfc6381_codec: rfc6381_codec.clone(),
            },
            Self::AVCVideo {
                width,
                height,
                frame_rate,
                is_interlaced,
                access_unit_counter,
                rfc6381_codec,
                timecode,
                pts_analyzer,
                ..
            } => StreamInfo::Video {
                width: *width,
                height: *height,
                frame_rate: if *frame_rate != 0.0 {
                    *frame_rate
                } else {
                    pts_analyzer.guess_frame_rate().unwrap_or(0.0)
                },
                frame_count: if *is_interlaced {
                    access_unit_counter.count() / 2
                } else {
                    access_unit_counter.count()
                },
                rfc6381_codec: rfc6381_codec.clone(),
                timecode: timecode.clone(),
                is_interlaced: *is_interlaced,
            },
            Self::HEVCVideo {
                width,
                height,
                frame_rate,
                access_unit_counter,
                rfc6381_codec,
                pts_analyzer,
                ..
            } => StreamInfo::Video {
                width: *width,
                height: *height,
                frame_rate: if *frame_rate != 0.0 {
                    *frame_rate
                } else {
                    pts_analyzer.guess_frame_rate().unwrap_or(0.0)
                },
                frame_count: access_unit_counter.count(),
                rfc6381_codec: rfc6381_codec.clone(),
                timecode: None,
                is_interlaced: false,
            },
            Self::Other(_) => StreamInfo::Other,
        }
    }

    fn handle_pes_packet(&mut self, packet: pes::Packet) -> Result<()> {
        match self {
            Self::ADTSAudio {
                channel_count,
                sample_rate,
                sample_count,
                rfc6381_codec,
                object_type_indication,
                ..
            } => {
                let mut data = packet.data.as_ref();
                while data.len() >= 7 {
                    let adts = AudioDataTransportStream::parse(data)?;
                    *sample_count += 1024;
                    *channel_count = adts.channel_count;
                    *sample_rate = adts.sample_rate;
                    if adts.mpeg_version == mpeg4::AudioDataTransportStreamMPEGVersion::MPEG4 {
                        *rfc6381_codec = Some(format!("mp4a.{:02x}.{}", object_type_indication, adts.mpeg4_audio_object_type));
                    }
                    data = &data[adts.frame_length..];
                }
            }
            Self::AVCVideo {
                width,
                height,
                frame_rate,
                rfc6381_codec,
                is_interlaced,
                access_unit_counter,
                timecode,
                last_timecode,
                last_vui_parameters,
                pts_analyzer,
                ..
            } => {
                match packet.header.optional_header.and_then(|h| h.pts) {
                    Some(pts) => pts_analyzer.write_pts(pts),
                    None => pts_analyzer.reset(),
                }

                use h264::Decode;

                for nalu in h264::iterate_annex_b(&packet.data) {
                    if nalu.is_empty() {
                        continue;
                    }

                    access_unit_counter.count_nalu(&nalu)?;

                    let nalu_type = nalu[0] & h264::NAL_UNIT_TYPE_MASK;
                    match nalu_type {
                        h264::NAL_UNIT_TYPE_SEQUENCE_PARAMETER_SET => {
                            let bs = h264::Bitstream::new(nalu.iter().copied());
                            let mut nalu = h264::NALUnit::decode(bs)?;
                            *rfc6381_codec = rfc6381::codec_from_h264_nalu(nalu.clone());
                            let mut rbsp = h264::Bitstream::new(&mut nalu.rbsp_byte);
                            let sps = h264::SequenceParameterSet::decode(&mut rbsp)?;
                            *is_interlaced = sps.frame_mbs_only_flag.0 == 0;
                            *width = sps.frame_cropping_rectangle_width() as _;
                            *height = sps.frame_cropping_rectangle_height() as _;
                            if sps.vui_parameters_present_flag.0 != 0
                                && sps.vui_parameters.timing_info_present_flag.0 != 0
                                && sps.vui_parameters.num_units_in_tick.0 != 0
                            {
                                *frame_rate =
                                    (sps.vui_parameters.time_scale.0 as f64 / (2.0 * sps.vui_parameters.num_units_in_tick.0 as f64) * 100.0).round() / 100.0;
                            } else {
                                // if the frame rate is later requested we'll try to guess it via the PTS analyzer
                                *frame_rate = 0.0;
                            }
                            *last_vui_parameters = Some(sps.vui_parameters);
                        }
                        h264::NAL_UNIT_TYPE_SUPPLEMENTAL_ENHANCEMENT_INFORMATION => {
                            let bs = h264::Bitstream::new(nalu.iter().copied());
                            let mut nalu = h264::NALUnit::decode(bs)?;

                            if let Some(vui_params) = &last_vui_parameters {
                                let mut rbsp = h264::Bitstream::new(&mut nalu.rbsp_byte);
                                let sei = h264::SEI::decode(&mut rbsp)?;

                                let mut pic_timings = vec![];
                                for message in sei.sei_message {
                                    let mut bs = h264::Bitstream::new(message.payload);
                                    let timing = h264::PicTiming::decode(&mut bs, vui_params)?;
                                    pic_timings.extend_from_slice(timing.timecodes.as_slice());
                                }

                                let timecodes: Vec<Timecode> = last_timecode.iter().cloned().collect();
                                let timecodes = pic_timings.iter().fold(timecodes, |mut timecodes, t| {
                                    let mut timecode = Timecode {
                                        hours: t.hours.0,
                                        minutes: t.minutes.0,
                                        seconds: t.seconds.0,
                                        frames: t.n_frames.0,
                                    };
                                    if let Some(previous_timecode) = timecodes.last() {
                                        if t.full_timestamp_flag.0 == 0 {
                                            if t.seconds_flag.0 == 0 {
                                                timecode.seconds = previous_timecode.seconds;
                                                timecode.minutes = previous_timecode.minutes;
                                                timecode.hours = previous_timecode.hours;
                                            } else if t.minutes_flag.0 == 0 {
                                                timecode.minutes = previous_timecode.minutes;
                                                timecode.hours = previous_timecode.hours;
                                            } else if t.hours_flag.0 == 0 {
                                                timecode.hours = previous_timecode.hours;
                                            }
                                        }
                                    }
                                    timecodes.push(timecode);
                                    timecodes
                                });
                                let last = timecodes.iter().last();
                                if let Some(last) = last {
                                    if timecode.is_none() {
                                        *timecode = Some(last.clone());
                                    }
                                    *last_timecode = Some(last.clone());
                                }
                            }
                        }
                        _ => {}
                    }
                }
            }
            Self::HEVCVideo {
                width,
                height,
                frame_rate,
                rfc6381_codec,
                access_unit_counter,
                pts_analyzer,
                ..
            } => {
                match packet.header.optional_header.and_then(|h| h.pts) {
                    Some(pts) => pts_analyzer.write_pts(pts),
                    None => pts_analyzer.reset(),
                }

                use h265::Decode;

                for nalu in h265::iterate_annex_b(&packet.data) {
                    if nalu.is_empty() {
                        continue;
                    }

                    access_unit_counter.count_nalu(&nalu)?;

                    let mut bs = h265::Bitstream::new(nalu.iter().copied());
                    let header = h265::NALUnitHeader::decode(&mut bs)?;

                    match header.nal_unit_type.0 {
                        h265::NAL_UNIT_TYPE_SPS_NUT => {
                            let bs = h265::Bitstream::new(nalu.iter().copied());
                            let mut nalu = h265::NALUnit::decode(bs)?;
                            *rfc6381_codec = rfc6381::codec_from_h265_nalu(nalu.clone());
                            let mut rbsp = h265::Bitstream::new(&mut nalu.rbsp_byte);
                            let sps = h265::SequenceParameterSet::decode(&mut rbsp)?;
                            *width = sps.croppedWidth() as _;
                            *height = sps.croppedHeight() as _;
                            if sps.vui_parameters_present_flag.0 != 0
                                && sps.vui_parameters.vui_timing_info_present_flag.0 != 0
                                && sps.vui_parameters.vui_num_units_in_tick.0 != 0
                            {
                                *frame_rate =
                                    (sps.vui_parameters.vui_time_scale.0 as f64 / sps.vui_parameters.vui_num_units_in_tick.0 as f64 * 100.0).round() / 100.0;
                            } else {
                                // if the frame rate is later requested we'll try to guess it via the PTS analyzer
                                *frame_rate = 0.0;
                            }
                        }
                        h265::NAL_UNIT_TYPE_VPS_NUT => {
                            let bs = h265::Bitstream::new(nalu.iter().copied());
                            let mut nalu = h265::NALUnit::decode(bs)?;
                            let mut rbsp = h265::Bitstream::new(&mut nalu.rbsp_byte);
                            let vps = h265::VideoParameterSet::decode(&mut rbsp)?;
                            if vps.vps_timing_info_present_flag.0 != 0 {
                                *frame_rate = match vps.vps_num_units_in_tick.0 {
                                    0 => 0.0,
                                    num_units_in_tick => (vps.vps_time_scale.0 as f64 / num_units_in_tick as f64 * 100.0).round() / 100.0,
                                };
                            }
                        }
                        _ => {}
                    }
                }
            }
            _ => {}
        }
        Ok(())
    }

    pub fn reset_timecode(&mut self) {
        if let Self::AVCVideo { timecode, .. } = self {
            *timecode = None;
        }
    }

    fn pes(&mut self) -> Option<&mut pes::Stream> {
        match self {
            Self::ADTSAudio { pes, .. } => Some(pes),
            Self::AVCVideo { pes, .. } => Some(pes),
            Self::HEVCVideo { pes, .. } => Some(pes),
            _ => None,
        }
    }

    pub fn write(&mut self, packet: &ts::Packet) -> Result<()> {
        if let Some(pes) = self.pes() {
            for packet in pes.write(packet)? {
                self.handle_pes_packet(packet)?;
            }
        }
        Ok(())
    }

    pub fn flush(&mut self) -> Result<()> {
        if let Some(pes) = self.pes() {
            for packet in pes.flush() {
                self.handle_pes_packet(packet)?;
            }
        }
        Ok(())
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum StreamInfo {
    Audio {
        channel_count: u32,
        sample_rate: u32,
        sample_count: u64,
        rfc6381_codec: Option<String>,
    },
    Video {
        width: u32,
        height: u32,
        frame_rate: f64,
        frame_count: u64,
        rfc6381_codec: Option<String>,
        timecode: Option<Timecode>,
        is_interlaced: bool,
    },
    Other,
}

#[allow(clippy::large_enum_variant)]
#[derive(Clone)]
enum PidState {
    Unused,
    Pat,
    Pmt,
    Pes { stream: Stream },
}

/// Analyzer processes packets in real-time, performing cheap analysis on the streams.
pub struct Analyzer {
    pids: Vec<PidState>,
    has_video: bool,
}

impl Analyzer {
    pub fn new() -> Self {
        Self {
            pids: {
                let mut v = vec![PidState::Unused; 0x10000];
                v[ts::PID_PAT as usize] = PidState::Pat;
                v
            },
            has_video: false,
        }
    }

    pub fn handle_packets(&mut self, packets: &[ts::Packet<'_>]) -> Result<()> {
        for packet in packets {
            self.handle_packet(packet)?;
        }
        Ok(())
    }

    pub fn is_pes(&self, pid: u16) -> bool {
        matches!(self.pids[pid as usize], PidState::Pes { .. })
    }

    pub fn stream(&self, pid: u16) -> Option<&Stream> {
        match &self.pids[pid as usize] {
            PidState::Pes { stream } => Some(stream),
            _ => None,
        }
    }

    pub fn is_video(&self, pid: u16) -> bool {
        match &self.pids[pid as usize] {
            PidState::Pes { stream } => stream.is_video(),
            _ => false,
        }
    }

    pub fn has_video(&self) -> bool {
        self.has_video
    }

    pub fn streams(&self) -> Vec<StreamInfo> {
        self.pids
            .iter()
            .filter_map(|pid| match pid {
                PidState::Pes { stream } => Some(stream.info()),
                _ => None,
            })
            .collect()
    }

    pub fn reset_timecodes(&mut self) {
        for pid in &mut self.pids {
            if let PidState::Pes { stream } = pid {
                stream.reset_timecode();
            }
        }
    }

    pub fn handle_packet(&mut self, packet: &ts::Packet<'_>) -> Result<()> {
        match &mut self.pids[packet.packet_id as usize] {
            PidState::Pat => {
                let table_sections = packet.decode_table_sections()?;
                let syntax_section = table_sections[0].decode_syntax_section()?;
                let pat = ts::PATData::decode(syntax_section.data)?;
                for entry in pat.entries {
                    self.pids[entry.program_map_pid as usize] = PidState::Pmt;
                }
            }
            PidState::Pmt => {
                let table_sections = packet.decode_table_sections()?;
                let syntax_section = table_sections[0].decode_syntax_section()?;
                let pmt = ts::PMTData::decode(syntax_section.data)?;
                for pes in pmt.elementary_stream_info {
                    match &mut self.pids[pes.elementary_pid as usize] {
                        PidState::Pes { .. } => {}
                        state => {
                            let stream = match pes.stream_type {
                                0x0f => Stream::ADTSAudio {
                                    pes: pes::Stream::new(),
                                    channel_count: 0,
                                    sample_rate: 0,
                                    sample_count: 0,
                                    object_type_indication: 0x40,
                                    rfc6381_codec: None,
                                },
                                0x1b => Stream::AVCVideo {
                                    pes: pes::Stream::new(),
                                    width: 0,
                                    height: 0,
                                    frame_rate: 0.0,
                                    is_interlaced: false,
                                    access_unit_counter: h264::AccessUnitCounter::new(),
                                    rfc6381_codec: None,
                                    last_vui_parameters: None,
                                    last_timecode: None,
                                    timecode: None,
                                    pts_analyzer: PTSAnalyzer::new(),
                                },
                                0x24 => Stream::HEVCVideo {
                                    pes: pes::Stream::new(),
                                    width: 0,
                                    height: 0,
                                    frame_rate: 0.0,
                                    access_unit_counter: h265::AccessUnitCounter::new(),
                                    rfc6381_codec: None,
                                    pts_analyzer: PTSAnalyzer::new(),
                                },
                                t => Stream::Other(t),
                            };
                            if stream.is_video() {
                                self.has_video = true;
                            }
                            *state = PidState::Pes { stream }
                        }
                    };
                }
            }
            PidState::Pes { ref mut stream } => {
                stream.write(packet)?;
            }
            PidState::Unused => {}
        }

        Ok(())
    }

    /// Streams with variable length PES packets should be flushed after the last packet is written
    /// to them. Otherwise, the last packet might not be evaluated.
    pub fn flush(&mut self) -> Result<()> {
        for pid in self.pids.iter_mut() {
            if let PidState::Pes { ref mut stream } = pid {
                stream.flush()?;
            }
        }
        Ok(())
    }
}

impl Default for Analyzer {
    fn default() -> Self {
        Self::new()
    }
}

/// PTSAnalyzer keeps a small history of PES presentation timestamps and can attempt to guess at
/// their frame rate.
#[derive(Clone)]
pub struct PTSAnalyzer {
    /// PES presentation timestamps: 90kHz, with no rollovers.
    timestamps: VecDeque<u64>,
}

impl Default for PTSAnalyzer {
    fn default() -> Self {
        Self::new()
    }
}

const PTS_ANALYZER_MAX_TIMESTAMPS: usize = 150;

impl PTSAnalyzer {
    pub fn new() -> Self {
        Self {
            timestamps: VecDeque::with_capacity(PTS_ANALYZER_MAX_TIMESTAMPS),
        }
    }

    pub fn write_pts(&mut self, mut pts: u64) {
        // Set the high bits of the PTS. If there's any sort of anamoly such as a large gap in
        // timestamps, the queues timestamps are reset and the PTS is left as-is.
        if let Some(&last) = self.timestamps.back() {
            let high_bits = if pts < 90000 && (last & 0x1ffffffff) > 0x1ffff0000 {
                (last >> 33).checked_add(1)
            } else if pts > 0x1ffff0000 && (last & 0x1ffffffff) < 90000 {
                (last >> 33).checked_sub(1)
            } else {
                Some(last >> 33)
            };
            match high_bits {
                Some(high_bits) => {
                    let adjusted_pts = pts | (high_bits << 33);
                    let abs_delta = adjusted_pts.max(last) - adjusted_pts.min(last);
                    if abs_delta > 90000 {
                        self.reset();
                    } else {
                        pts = adjusted_pts;
                    }
                }
                None => self.reset(),
            }
        }

        // Push the PTS, limiting the queue size.
        if self.timestamps.len() >= PTS_ANALYZER_MAX_TIMESTAMPS {
            self.timestamps.pop_front();
        }
        self.timestamps.push_back(pts);
    }

    pub fn reset(&mut self) {
        self.timestamps.clear()
    }

    /// Makes a guess at a video's frame rate. This should really only be used as a last resort. If
    /// the presentation timestamps were set precisely it should be accurate, but if the timestamps
    /// have jitter, e.g. due to being set to wall-clock times, the guess may be off. For those
    /// cases, it has a bias towards returning 29.97 or 59.94.
    pub fn guess_frame_rate(&self) -> Option<f64> {
        const MIN_TIMESTAMP_COUNT: usize = 16;
        const MAX_B_FRAMES: usize = 3;

        if self.timestamps.len() < MIN_TIMESTAMP_COUNT {
            return None;
        }

        let mut timestamps = self.timestamps.clone();
        timestamps.make_contiguous().sort_unstable();
        // ignore the most oldest and most recent timestamps so b-frames don't throw us off
        let timestamps: Vec<_> = timestamps
            .into_iter()
            .skip(MAX_B_FRAMES)
            .take(self.timestamps.len() - 2 * MAX_B_FRAMES)
            .collect();

        let mut min_delta = u64::MAX;
        let mut max_delta = u64::MIN;
        let mut sum_delta = 0;

        {
            let mut prev = None;
            for &pts in &timestamps {
                if let Some(prev) = prev {
                    let delta = pts - prev;
                    min_delta = min_delta.min(delta);
                    max_delta = max_delta.max(delta);
                    sum_delta += delta;
                }
                prev = Some(pts);
            }
        }

        let avg_delta = sum_delta / (timestamps.len() as u64 - 1);
        if avg_delta == 0 {
            return None;
        }

        let fps = 90000.0 / (avg_delta as f64);

        if max_delta - min_delta > 5 || (0.5 - fps.fract()).abs() < 0.48 {
            // if the deltas were inconsistent (e.g. due to wallclock timestamps) or not a round
            // number and this was nearly 30 or 60 fps, we should assume 29.97 or 59.94
            if (fps - 29.97).abs() < 5.0 {
                return Some(29.97);
            } else if (fps - 59.94).abs() < 5.0 {
                return Some(59.94);
            }
        }

        Some((fps * 100.0).round() / 100.0)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::{fs::File, io::Read};

    #[tokio::test]
    async fn test_analyzer_h264() {
        let mut analyzer = Analyzer::new();

        {
            let mut f = File::open("src/testdata/h264.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            let packets = ts::decode_packets(&buf).unwrap();
            analyzer.handle_packets(&packets).unwrap();
            analyzer.flush().unwrap();
        }

        assert!(analyzer.has_video());
        assert_eq!(
            analyzer.streams(),
            vec![
                StreamInfo::Video {
                    width: 1280,
                    height: 720,
                    frame_rate: 59.94,
                    frame_count: 600,
                    rfc6381_codec: Some("avc1.7a0020".to_string()),
                    timecode: None,
                    is_interlaced: false,
                },
                StreamInfo::Audio {
                    channel_count: 2,
                    sample_rate: 48_000,
                    sample_count: 481_280,
                    rfc6381_codec: Some("mp4a.40.2".to_string()),
                }
            ]
        );
    }

    #[tokio::test]
    async fn test_analyzer_h264_8k() {
        let mut analyzer = Analyzer::new();

        {
            let mut f = File::open("src/testdata/h264-8k.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            let packets = ts::decode_packets(&buf).unwrap();
            analyzer.handle_packets(&packets).unwrap();
            analyzer.flush().unwrap();
        }

        assert!(analyzer.has_video());
        assert_eq!(
            analyzer.streams(),
            vec![
                StreamInfo::Video {
                    width: 7680,
                    height: 4320,
                    frame_rate: 29.97,
                    frame_count: 33,
                    rfc6381_codec: Some("avc1.42003c".to_string()),
                    timecode: None,
                    is_interlaced: false,
                },
                StreamInfo::Audio {
                    channel_count: 2,
                    sample_rate: 48_000,
                    sample_count: 81_920,
                    rfc6381_codec: Some("mp4a.40.2".to_string()),
                }
            ]
        );
    }

    #[tokio::test]
    async fn test_analyzer_h265() {
        let mut analyzer = Analyzer::new();

        {
            let mut f = File::open("src/testdata/h265.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            let packets = ts::decode_packets(&buf).unwrap();
            analyzer.handle_packets(&packets).unwrap();
            analyzer.flush().unwrap();
        }

        assert!(analyzer.has_video());
        assert_eq!(
            analyzer.streams(),
            vec![
                StreamInfo::Video {
                    width: 1280,
                    height: 720,
                    frame_rate: 59.94,
                    frame_count: 600,
                    rfc6381_codec: Some("hvc1.4.10.L120.9D.08".to_string()),
                    timecode: None,
                    is_interlaced: false,
                },
                StreamInfo::Audio {
                    channel_count: 2,
                    sample_rate: 48_000,
                    sample_count: 481_280,
                    rfc6381_codec: Some("mp4a.40.2".to_string()),
                }
            ]
        );
    }

    #[tokio::test]
    async fn test_analyzer_h265_8k() {
        let mut analyzer = Analyzer::new();

        {
            let mut f = File::open("src/testdata/h265-8k.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            let packets = ts::decode_packets(&buf).unwrap();
            analyzer.handle_packets(&packets).unwrap();
            analyzer.flush().unwrap();
        }

        assert!(analyzer.has_video());
        assert_eq!(
            analyzer.streams(),
            vec![
                StreamInfo::Video {
                    width: 7680,
                    height: 4320,
                    frame_rate: 59.94,
                    frame_count: 31,
                    rfc6381_codec: Some("hvc1.2.6.L180.B0".to_string()),
                    timecode: None,
                    is_interlaced: false,
                },
                StreamInfo::Audio {
                    channel_count: 2,
                    sample_rate: 48000,
                    sample_count: 49152,
                    rfc6381_codec: Some("mp4a.40.2".to_string()),
                }
            ]
        );
    }

    #[tokio::test]
    async fn test_analyzer_h265_8k_hq() {
        let mut analyzer = Analyzer::new();

        {
            let mut f = File::open("src/testdata/h265-8k-hq.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            let packets = ts::decode_packets(&buf).unwrap();
            analyzer.handle_packets(&packets).unwrap();
            analyzer.flush().unwrap();
        }

        assert!(analyzer.has_video());
        assert_eq!(
            analyzer.streams(),
            vec![StreamInfo::Video {
                width: 7680,
                height: 4320,
                frame_rate: 29.97,
                frame_count: 30,
                rfc6381_codec: Some("hvc1.2.6.L180.B0".to_string()),
                timecode: None,
                is_interlaced: false,
            },]
        );
    }

    #[tokio::test]
    async fn test_analyzer_h265_cropped() {
        let mut analyzer = Analyzer::new();

        {
            let mut f = File::open("src/testdata/h265-cropped.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            let packets = ts::decode_packets(&buf).unwrap();
            analyzer.handle_packets(&packets).unwrap();
            analyzer.flush().unwrap();
        }

        assert!(analyzer.has_video());
        assert_eq!(
            analyzer.streams(),
            vec![
                StreamInfo::Video {
                    width: 1920,
                    height: 1080,
                    frame_rate: 59.94,
                    frame_count: 39,
                    rfc6381_codec: Some("hvc1.1.6.L153.B0".to_string()),
                    timecode: None,
                    is_interlaced: false,
                },
                StreamInfo::Audio {
                    channel_count: 2,
                    sample_rate: 48000,
                    sample_count: 31744,
                    rfc6381_codec: Some("mp4a.40.2".to_string())
                }
            ]
        );
    }

    #[tokio::test]
    async fn test_analyzer_h265_8k_wallclock_ts() {
        let mut analyzer = Analyzer::new();

        {
            let mut f = File::open("src/testdata/h265-8k-wallclock-ts.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            let packets = ts::decode_packets(&buf).unwrap();
            analyzer.handle_packets(&packets).unwrap();
            analyzer.flush().unwrap();
        }

        assert!(analyzer.has_video());
        assert_eq!(
            analyzer.streams(),
            vec![StreamInfo::Video {
                width: 7680,
                height: 4320,
                frame_rate: 29.97,
                frame_count: 31,
                rfc6381_codec: Some("hvc1.2.6.L180.B0".to_string()),
                timecode: None,
                is_interlaced: false,
            },]
        );
    }

    #[tokio::test]
    async fn test_analyzer_program() {
        let mut analyzer = Analyzer::new();

        {
            let mut f = File::open("src/testdata/program.ts").unwrap();
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).unwrap();
            let packets = ts::decode_packets(&buf).unwrap();
            analyzer.handle_packets(&packets).unwrap();
            analyzer.flush().unwrap();
        }

        assert!(analyzer.has_video());
        assert_eq!(
            analyzer.streams(),
            vec![
                StreamInfo::Video {
                    width: 1920,
                    height: 1080,
                    frame_rate: 29.97,
                    frame_count: 152,
                    rfc6381_codec: Some("avc1.4d4028".to_string()),
                    timecode: Some(Timecode {
                        hours: 18,
                        minutes: 57,
                        seconds: 26,
                        frames: 2
                    }),
                    is_interlaced: true,
                },
                StreamInfo::Audio {
                    channel_count: 2,
                    sample_rate: 48_000,
                    sample_count: 243_712,
                    rfc6381_codec: Some("mp4a.40.2".to_string()),
                }
            ]
        );
    }

    #[test]
    fn test_pts_analyzer_b_frames() {
        let mut analyzer = PTSAnalyzer::new();
        for i in 0..500 {
            analyzer.write_pts(
                match i % 3 {
                    0 => i,
                    1 => i + 1,
                    _ => i - 1,
                } * 3000,
            );
            if i > 30 {
                assert_eq!(analyzer.guess_frame_rate(), Some(30.0));
            }
        }
    }
}
