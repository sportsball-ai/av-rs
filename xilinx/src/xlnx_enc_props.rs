use simple_error::{bail, SimpleError};

use crate::{strcpy_to_arr_i8, sys::*};

pub const MAX_ENC_PARAMS: usize = 6;

const ENC_OPTIONS_PARAM_NAME: &[u8] = b"enc_options\0";
const LATENCY_LOGGING_PARAM_NAME: &[u8] = b"latency_logging\0";
const ENABLE_HW_IN_BUF_PARAM_NAME: &[u8] = b"enable_hw_in_buf\0";

pub const ENC_H264_BASELINE: i32 = 66;
pub const ENC_H264_MAIN: i32 = 77;
pub const ENC_H264_HIGH: i32 = 100;
pub const ENC_HEVC_MAIN: i32 = 0;
pub const ENC_HEVC_MAIN_INTRA: i32 = 1;

pub enum XlnxAspectRatio {
    AspectRatio4x3,
    AspectRatio16x9,
    AspectRatioNone,
    AspectRatioAuto,
}

pub const CODEC_ID_HEVC: i32 = 1;
pub const CODEC_ID_H264: i32 = 0;

pub struct XlnxEncoderProperties {
    pub cpb_size: f32,
    pub initial_delay: f32,
    pub max_bitrate: i64,
    pub bit_rate: i64,
    pub width: i32,
    pub height: i32,
    pub framerate: XmaFraction,
    pub gop_size: i32,
    pub slice_qp: i32,
    pub min_qp: i32,
    pub max_qp: i32,
    pub codec_id: i32,
    pub control_rate: i32,
    pub custom_rc: i32,
    pub gop_mode: i32,
    pub gdr_mode: i32,
    pub num_bframes: u32,
    pub idr_period: u32,
    pub profile: i32,
    pub level: i32,
    pub tier: i32,
    pub num_slices: i32,
    pub dependent_slice: bool,
    pub slice_size: i32,
    pub temporal_aq: i32,
    pub spatial_aq: i32,
    pub spatial_aq_gain: i32,
    pub qp_mode: i32,
    pub filler_data: bool,
    pub aspect_ratio: XlnxAspectRatio,
    pub scaling_list: i32,
    pub entropy_mode: i32,
    pub loop_filter: bool,
    pub constrained_intra_pred: bool,
    pub prefetch_buffer: bool,
    pub tune_metrics: bool,
    pub num_cores: i32,
    pub latency_logging: i32,
    pub enable_hw_buf: u32,
    pub enc_options: String,
    pub enc_options_ptr: *const u8,
}

fn xlnx_fill_enc_params(enc_props: &mut Box<XlnxEncoderProperties>, enc_params: &mut Box<[XmaParameter; MAX_ENC_PARAMS]>) {
    enc_params[0].name = ENC_OPTIONS_PARAM_NAME.as_ptr() as *mut i8;
    enc_params[0].type_ = XmaDataType_XMA_STRING;
    enc_params[0].length = enc_props.enc_options.len() as u64;
    enc_params[0].value = &enc_props.enc_options_ptr as *const _ as *mut std::ffi::c_void;

    enc_params[1].name = LATENCY_LOGGING_PARAM_NAME.as_ptr() as *mut i8;
    enc_params[1].type_ = XmaDataType_XMA_UINT32;
    enc_params[1].length = 4;
    enc_params[1].value = &mut enc_props.latency_logging as *mut _ as *mut std::ffi::c_void;

    enc_params[2].name = ENABLE_HW_IN_BUF_PARAM_NAME.as_ptr() as *mut i8;
    enc_params[2].type_ = XmaDataType_XMA_UINT32;
    enc_params[2].length = 4;
    enc_params[2].value = &mut enc_props.enable_hw_buf as *mut _ as *mut std::ffi::c_void;
}

pub fn xlnx_create_xma_enc_props(
    enc_props: &mut Box<XlnxEncoderProperties>,
    enc_params: &mut Box<[XmaParameter; MAX_ENC_PARAMS]>,
) -> Result<Box<XmaEncoderProperties>, SimpleError> {
    let xma_enc_props: XmaEncoderProperties = Default::default();
    let mut xma_enc_props = Box::new(xma_enc_props);

    strcpy_to_arr_i8(&mut xma_enc_props.hwvendor_string, "MPSoC")?;
    xma_enc_props.hwencoder_type = XmaEncoderType_XMA_MULTI_ENCODER_TYPE;
    xma_enc_props.param_cnt = 3_u32;
    xma_enc_props.params = enc_params.as_mut_ptr();
    xma_enc_props.format = XmaFormatType_XMA_VCU_NV12_FMT_TYPE;
    xma_enc_props.bits_per_pixel = 8;
    xma_enc_props.width = enc_props.width;
    xma_enc_props.height = enc_props.height;
    xma_enc_props.rc_mode = 0;
    xma_enc_props.framerate = enc_props.framerate;
    xma_enc_props.lookahead_depth = 0;

    // Begin filling encoder options
    let rate_ctrl_mode = match enc_props.control_rate {
        1 => "CBR",
        2 => "VBR",
        3 => "LOW_LATENCY",
        _ => "CONST_QP",
    };

    let framerate = format!("{}/{}", enc_props.framerate.numerator, enc_props.framerate.denominator);

    let slice_qp = if enc_props.slice_qp == -1 {
        "AUTO".to_string()
    } else {
        format!("{}", enc_props.slice_qp)
    };

    let gop_ctrl_mode = match enc_props.gop_mode {
        1 => "PYRAMIDAL_GOP",
        2 => "LOW_DELAY_P",
        3 => "LOW_DELAY_B",
        _ => "DEFAULT_GOP",
    };

    let gdr_mode = match enc_props.gdr_mode {
        1 => "GDR_VERTICAL",
        2 => "GDR_HORIZONTAL",
        _ => "DISABLE",
    };

    let profile = match enc_props.codec_id {
        // h264
        CODEC_ID_H264 => match enc_props.profile {
            ENC_H264_BASELINE => "AVC_BASELINE",
            ENC_H264_MAIN => "AVC_MAIN",
            ENC_H264_HIGH => "AVC_HIGH",
            _ => bail!("invalid profile {} specified to H264 xilinx encoder", enc_props.profile),
        },
        // h265
        CODEC_ID_HEVC => match enc_props.profile {
            ENC_HEVC_MAIN => "HEVC_MAIN",
            ENC_HEVC_MAIN_INTRA => "HEVC_MAIN_INTRA",
            _ => bail!("invalid profile {} specified to HEVC xilinx encoder", enc_props.profile),
        },
        _ => bail!("incompatible codec_id {}", enc_props.codec_id),
    };

    let mut is_level_found = true;
    let mut level = match enc_props.level {
        10 => "1",
        20 => "2",
        21 => "2.1",
        30 => "3",
        31 => "3.1",
        40 => "4",
        41 => "4.1",
        50 => "5",
        51 => "5.1",
        _ => {
            is_level_found = false;
            "1"
        }
    };

    if !is_level_found {
        match enc_props.codec_id {
            CODEC_ID_H264 => {
                level = match enc_props.level {
                    11 => "1.1",
                    12 => "1.2",
                    13 => "1.3",
                    22 => "2.2",
                    32 => "3.2",
                    42 => "4.2",
                    52 => "5.2",
                    _ => bail!("invalid H264 codec level value {}", enc_props.level),
                }
            }
            CODEC_ID_HEVC => bail!("invalid HEVC codec level value{}", enc_props.level),
            _ => {}
        }
    }

    let tier = if enc_props.tier == 1 { "HIGH_TIER" } else { "MAIN_TIER" };

    let mut qp_ctrl_mode = match enc_props.qp_mode {
        1 => "UNIFORM_QP",
        2 => "AUTO_QP",
        _ => "LOAD_QP | RELATIVE_QP",
    };

    let dependent_slice = if enc_props.dependent_slice { "TRUE" } else { "FALSE" };

    let filler_data = if enc_props.filler_data { "ENABLE" } else { "DISABLE" };

    let aspect_ratio = match enc_props.aspect_ratio {
        XlnxAspectRatio::AspectRatio4x3 => "ASPECT_RATIO_4_3",
        XlnxAspectRatio::AspectRatio16x9 => "ASPECT_RATIO_16_9",
        XlnxAspectRatio::AspectRatioNone => "ASPECT_RATIO_NONE",
        XlnxAspectRatio::AspectRatioAuto => "ASPECT_RATIO_AUTO",
    };

    let color_space = "COLOUR_DESC_UNSPECIFIED";

    let mut scaling_list = if enc_props.scaling_list == 0 { "FLAT" } else { "DEFAULT" };

    let loop_filter = if enc_props.loop_filter { "ENABLE" } else { "DISABLE" };

    let entropy_mode = if enc_props.entropy_mode == 0 { "MODE_CAVLC" } else { "MODE_CABAC" };

    let const_intra_pred = if enc_props.constrained_intra_pred { "ENABLE" } else { "DISABLE" };

    let lambda_ctrl_mode = "DEFAULT_LDA";

    let prefetch_buffer = if enc_props.prefetch_buffer { "ENABLE" } else { "DISABLE" };

    if enc_props.tune_metrics {
        scaling_list = "FLAT";
        qp_ctrl_mode = "UNIFORM_QP";
    }

    let width = enc_props.width;
    let height = enc_props.height;
    let bit_rate = enc_props.bit_rate;
    let max_bitrate = enc_props.max_bitrate;
    let max_qp = enc_props.max_qp;
    let min_qp = enc_props.min_qp;
    let cpb_size = enc_props.cpb_size;
    let initial_delay = enc_props.initial_delay;
    let gop_size = enc_props.gop_size;
    let num_bframes = enc_props.num_bframes;
    let idr_period = enc_props.idr_period;
    let num_slices = enc_props.num_slices;
    let slice_size = enc_props.slice_size;
    let num_cores = enc_props.num_cores;

    let enc_options = if enc_props.codec_id == CODEC_ID_HEVC {
        format!(
            "[INPUT]\n\
        Width = {width}\n\
        Height = {height}\n\
        [RATE_CONTROL]\n\
        RateCtrlMode = {rate_ctrl_mode}\n\
        FrameRate = {framerate}\n\
        BitRate = {bit_rate}\n\
        MaxBitRate = {max_bitrate}\n\
        SliceQP = {slice_qp}\n\
        MaxQP = {max_qp}\n\
        MinQP = {min_qp}\n\
        CPBSize = {cpb_size}\n\
        InitialDelay = {initial_delay}\n\
        [GOP]\n\
        GopCtrlMode = {gop_ctrl_mode}\n\
        Gop.GdrMode = {gdr_mode}\n\
        Gop.Length = {gop_size}\n\
        Gop.NumB = {num_bframes}\n\
        Gop.FreqIDR = {idr_period}\n\
        [SETTINGS]\n\
        Profile = {profile}\n\
        Level = {level}\n\
        Tier = {tier}\n\
        ChromaMode = CHROMA_4_2_0\n\
        BitDepth = 8\n\
        NumSlices = {num_slices}\n\
        QPCtrlMode = {qp_ctrl_mode}\n\
        SliceSize = {slice_size}\n\
        DependentSlice = {dependent_slice}\n\
        EnableFillerData = {filler_data}\n\
        AspectRatio = {aspect_ratio}\n\
        ColourDescription = {color_space}\n\
        ScalingList = {scaling_list}\n\
        LoopFilter = {loop_filter}\n\
        ConstrainedIntraPred = {const_intra_pred}\n\
        LambdaCtrlMode = {lambda_ctrl_mode}\n\
        CacheLevel2 = {prefetch_buffer}\n\
        NumCore = {num_cores}\n\0"
        )
    } else {
        format!(
            "[INPUT]\n\
        Width = {width}\n\
        Height = {height}\n\
        [RATE_CONTROL]\n\
        RateCtrlMode = {rate_ctrl_mode}\n\
        FrameRate = {framerate}\n\
        BitRate = {bit_rate}\n\
        MaxBitRate = {max_bitrate}\n\
        SliceQP = {slice_qp}\n\
        MaxQP = {max_qp}\n\
        MinQP = {min_qp}\n\
        CPBSize = {cpb_size}\n\
        InitialDelay = {initial_delay}\n\
        [GOP]\n\
        GopCtrlMode = {gop_ctrl_mode}\n\
        Gop.GdrMode = {gdr_mode}\n\
        Gop.Length = {gop_size}\n\
        Gop.NumB = {num_bframes}\n\
        Gop.FreqIDR = {idr_period}\n\
        [SETTINGS]\n\
        Profile = {profile}\n\
        Level = {level}\n\
        ChromaMode = CHROMA_4_2_0\n\
        BitDepth = 8\n\
        NumSlices = {num_slices}\n\
        QPCtrlMode = {qp_ctrl_mode}\n\
        SliceSize = {slice_size}\n\
        EnableFillerData = {filler_data}\n\
        AspectRatio = {aspect_ratio}\n\
        ColourDescription = {color_space}\n\
        ScalingList = {scaling_list}\n\
        EntropyMode = {entropy_mode}\n\
        LoopFilter = {loop_filter}\n\
        ConstrainedIntraPred = {const_intra_pred}\n\
        LambdaCtrlMode = {lambda_ctrl_mode}\n\
        CacheLevel2 = {prefetch_buffer}\n\
        NumCore = {num_cores}\n\0"
        )
    };

    enc_props.enc_options = enc_options;
    enc_props.enc_options_ptr = enc_props.enc_options.as_ptr();
    xlnx_fill_enc_params(enc_props, enc_params);

    Ok(xma_enc_props)
}
