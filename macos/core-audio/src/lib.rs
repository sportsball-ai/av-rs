#![cfg(target_os = "macos")]

#[macro_use]
extern crate bitflags;

pub mod sys {
    #![allow(
        deref_nullptr,
        non_snake_case,
        non_upper_case_globals,
        non_camel_case_types,
        clippy::unreadable_literal,
        clippy::cognitive_complexity
    )]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

#[derive(Clone, Copy, Debug)]
pub enum AudioFormat {
    LinearPCM,
    AC3,
    AppleIMA4,
    MPEG4AAC,
    MPEG4CELP,
    MPEG4HVXC,
    MPEG4TwinVQ,
    MACE3,
    MACE6,
    ULaw,
    ALaw,
    QDesign,
    QDesign2,
    QUALCOMM,
    MPEGLayer1,
    MPEGLayer2,
    MPEGLayer3,
    TimeCode,
    MIDIStream,
    ParameterValueStream,
    AppleLossless,
    MPEG4AACHE,
    MPEG4AACLD,
    MPEG4AACELD,
    MPEG4AACELDSBR,
    MPEG4AACHEV2,
    MPEG4AACSpatial,
    AMR,
    Audible,
    ILBC,
    DVIIntelIMA,
    MicrosoftGSM,
    AES3,
    AMRWB,
    EnhancedAC3,
    MPEG4AACELDV2,
    FLAC,
    MPEGDUSAC,
    Opus,
}

impl AudioFormat {
    pub fn as_sys(&self) -> u32 {
        match self {
            Self::LinearPCM => sys::kAudioFormatLinearPCM,
            Self::AC3 => sys::kAudioFormatAC3,
            Self::AppleIMA4 => sys::kAudioFormatAppleIMA4,
            Self::MPEG4AAC => sys::kAudioFormatMPEG4AAC,
            Self::MPEG4CELP => sys::kAudioFormatMPEG4CELP,
            Self::MPEG4HVXC => sys::kAudioFormatMPEG4HVXC,
            Self::MPEG4TwinVQ => sys::kAudioFormatMPEG4TwinVQ,
            Self::MACE3 => sys::kAudioFormatMACE3,
            Self::MACE6 => sys::kAudioFormatMACE6,
            Self::ULaw => sys::kAudioFormatULaw,
            Self::ALaw => sys::kAudioFormatALaw,
            Self::QDesign => sys::kAudioFormatQDesign,
            Self::QDesign2 => sys::kAudioFormatQDesign2,
            Self::QUALCOMM => sys::kAudioFormatQUALCOMM,
            Self::MPEGLayer1 => sys::kAudioFormatMPEGLayer1,
            Self::MPEGLayer2 => sys::kAudioFormatMPEGLayer2,
            Self::MPEGLayer3 => sys::kAudioFormatMPEGLayer3,
            Self::TimeCode => sys::kAudioFormatTimeCode,
            Self::MIDIStream => sys::kAudioFormatMIDIStream,
            Self::ParameterValueStream => sys::kAudioFormatParameterValueStream,
            Self::AppleLossless => sys::kAudioFormatAppleLossless,
            Self::MPEG4AACHE => sys::kAudioFormatMPEG4AAC_HE,
            Self::MPEG4AACLD => sys::kAudioFormatMPEG4AAC_LD,
            Self::MPEG4AACELD => sys::kAudioFormatMPEG4AAC_ELD,
            Self::MPEG4AACELDSBR => sys::kAudioFormatMPEG4AAC_ELD_SBR,
            Self::MPEG4AACHEV2 => sys::kAudioFormatMPEG4AAC_HE_V2,
            Self::MPEG4AACSpatial => sys::kAudioFormatMPEG4AAC_Spatial,
            Self::AMR => sys::kAudioFormatAMR,
            Self::Audible => sys::kAudioFormatAudible,
            Self::ILBC => sys::kAudioFormatiLBC,
            Self::DVIIntelIMA => sys::kAudioFormatDVIIntelIMA,
            Self::MicrosoftGSM => sys::kAudioFormatMicrosoftGSM,
            Self::AES3 => sys::kAudioFormatAES3,
            Self::AMRWB => sys::kAudioFormatAMR_WB,
            Self::EnhancedAC3 => sys::kAudioFormatEnhancedAC3,
            Self::MPEG4AACELDV2 => sys::kAudioFormatMPEG4AAC_ELD_V2,
            Self::FLAC => sys::kAudioFormatFLAC,
            Self::MPEGDUSAC => sys::kAudioFormatMPEGD_USAC,
            Self::Opus => sys::kAudioFormatOpus,
        }
    }
}

bitflags! {
    pub struct AudioFormatFlags: u32 {
        const IS_FLOAT = sys::kAudioFormatFlagIsFloat;
        const IS_BIG_ENDIAN = sys::kAudioFormatFlagIsBigEndian;
        const IS_SIGNED_INTEGER = sys::kAudioFormatFlagIsSignedInteger;
        const IS_PACKED = sys::kAudioFormatFlagIsPacked;
        const IS_ALIGNED_HIGH = sys::kAudioFormatFlagIsAlignedHigh;
        const IS_NON_INTERLEAVED = sys::kAudioFormatFlagIsNonInterleaved;
        const IS_NON_MIXABLE = sys::kAudioFormatFlagIsNonMixable;
        const LINEAR_PCM_IS_FLOAT = sys::kLinearPCMFormatFlagIsFloat;
        const LINEAR_PCM_IS_BIG_ENDIAN = sys::kLinearPCMFormatFlagIsBigEndian;
        const LINEAR_PCM_IS_SIGNED_INTEGER = sys::kLinearPCMFormatFlagIsSignedInteger;
        const LINEAR_PCM_IS_PACKED = sys::kLinearPCMFormatFlagIsPacked;
        const LINEAR_PCM_IS_ALIGNED_HIGH = sys::kLinearPCMFormatFlagIsAlignedHigh;
        const LINEAR_PCM_IS_NON_INTERLEAVED = sys::kLinearPCMFormatFlagIsNonInterleaved;
        const LINEAR_PCM_IS_NON_MIXABLE = sys::kLinearPCMFormatFlagIsNonMixable;
    }
}

#[derive(Clone, Debug)]
pub struct AudioStreamBasicDescription {
    pub bits_per_channel: u32,
    pub bytes_per_frame: u32,
    pub bytes_per_packet: u32,
    pub channels_per_frame: u32,
    pub format_flags: AudioFormatFlags,
    pub format: AudioFormat,
    pub frames_per_packet: u32,
    pub sample_rate: f64,
}

impl From<AudioStreamBasicDescription> for sys::AudioStreamBasicDescription {
    fn from(asbd: AudioStreamBasicDescription) -> Self {
        Self {
            mBitsPerChannel: asbd.bits_per_channel,
            mBytesPerFrame: asbd.bytes_per_frame,
            mBytesPerPacket: asbd.bytes_per_packet,
            mChannelsPerFrame: asbd.channels_per_frame,
            mFormatFlags: asbd.format_flags.bits(),
            mFormatID: asbd.format.as_sys(),
            mFramesPerPacket: asbd.frames_per_packet,
            mReserved: 0,
            mSampleRate: asbd.sample_rate,
        }
    }
}

#[derive(Clone, Debug)]
pub struct AudioStreamPacketDescription {
    pub length: usize,
    pub offset: usize,
    pub variable_frames: usize,
}

impl From<AudioStreamPacketDescription> for sys::AudioStreamPacketDescription {
    fn from(aspd: AudioStreamPacketDescription) -> Self {
        Self {
            mDataByteSize: aspd.length as _,
            mStartOffset: aspd.offset as _,
            mVariableFramesInPacket: aspd.variable_frames as _,
        }
    }
}
