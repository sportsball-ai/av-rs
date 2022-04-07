use super::sys;

#[derive(Clone, Copy, Debug)]
pub enum VideoCodecType {
    AppleProRes422,
    AppleProRes422Hq,
    AppleProRes422Lt,
    AppleProRes422Proxy,
    AppleProRes4444,
    AppleProRes4444Xq,
    AppleProResRaw,
    AppleProResRawHq,
    Animation,
    Cinepak,
    Jpeg,
    JpegOpenDml,
    SorensonVideo,
    SorensonVideo3,
    H263,
    H264,
    Hevc,
    HevcWithAlpha,
    Mpeg4Video,
    Mpeg2Video,
    Mpeg1Video,
    Vp9,
}

impl From<VideoCodecType> for sys::CMVideoCodecType {
    fn from(t: VideoCodecType) -> Self {
        match t {
            VideoCodecType::AppleProRes422 => sys::kCMVideoCodecType_AppleProRes422,
            VideoCodecType::AppleProRes422Hq => sys::kCMVideoCodecType_AppleProRes422HQ,
            VideoCodecType::AppleProRes422Lt => sys::kCMVideoCodecType_AppleProRes422LT,
            VideoCodecType::AppleProRes422Proxy => sys::kCMVideoCodecType_AppleProRes422Proxy,
            VideoCodecType::AppleProRes4444 => sys::kCMVideoCodecType_AppleProRes4444,
            VideoCodecType::AppleProRes4444Xq => sys::kCMVideoCodecType_AppleProRes4444XQ,
            VideoCodecType::AppleProResRaw => sys::kCMVideoCodecType_AppleProResRAW,
            VideoCodecType::AppleProResRawHq => sys::kCMVideoCodecType_AppleProResRAWHQ,
            VideoCodecType::Animation => sys::kCMVideoCodecType_Animation,
            VideoCodecType::Cinepak => sys::kCMVideoCodecType_Cinepak,
            VideoCodecType::Jpeg => sys::kCMVideoCodecType_JPEG,
            VideoCodecType::JpegOpenDml => sys::kCMVideoCodecType_JPEG_OpenDML,
            VideoCodecType::SorensonVideo => sys::kCMVideoCodecType_SorensonVideo,
            VideoCodecType::SorensonVideo3 => sys::kCMVideoCodecType_SorensonVideo3,
            VideoCodecType::H263 => sys::kCMVideoCodecType_H263,
            VideoCodecType::H264 => sys::kCMVideoCodecType_H264,
            VideoCodecType::Hevc => sys::kCMVideoCodecType_HEVC,
            VideoCodecType::HevcWithAlpha => sys::kCMVideoCodecType_HEVCWithAlpha,
            VideoCodecType::Mpeg4Video => sys::kCMVideoCodecType_MPEG4Video,
            VideoCodecType::Mpeg2Video => sys::kCMVideoCodecType_MPEG2Video,
            VideoCodecType::Mpeg1Video => sys::kCMVideoCodecType_MPEG1Video,
            VideoCodecType::Vp9 => sys::kCMVideoCodecType_VP9,
        }
    }
}
