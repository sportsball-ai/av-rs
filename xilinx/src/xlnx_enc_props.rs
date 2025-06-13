use crate::{strcpy_to_arr_i8, sys::*, Error, XrmCodec, XrmPlugin};

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
}

pub struct XlnxXmaEncoderProperties {
    // XXX: The drop order of these fields matters!
    inner: XmaEncoderProperties,
    // `inner` references `_params`.
    _params: Box<[XmaParameter; MAX_ENC_PARAMS]>,
    // `_params` references `_props`.
    _props: Box<XlnxEncoderProperties>,
    // `_params` references `_enc_options_ptr`.
    _enc_options_ptr: Box<*const u8>,
    // `_enc_options_ptr` references `_enc_options`.
    _enc_options: String,
}

unsafe impl Send for XlnxXmaEncoderProperties {}

impl TryFrom<XlnxEncoderProperties> for XlnxXmaEncoderProperties {
    type Error = Error;

    fn try_from(props: XlnxEncoderProperties) -> Result<Self, Error> {
        let mut props = Box::new(props);
        let mut params: Box<[XmaParameter; MAX_ENC_PARAMS]> = Default::default();

        let mut inner = XmaEncoderProperties::default();

        strcpy_to_arr_i8(&mut inner.hwvendor_string, "MPSoC").expect("MPSoC should definitely fit");
        inner.hwencoder_type = XmaEncoderType_XMA_MULTI_ENCODER_TYPE;
        inner.param_cnt = 3_u32;
        inner.params = params.as_mut_ptr();
        inner.format = XmaFormatType_XMA_VCU_NV12_FMT_TYPE;
        inner.bits_per_pixel = 8;
        inner.width = props.width;
        inner.height = props.height;
        inner.rc_mode = 0;
        inner.framerate = props.framerate;
        inner.lookahead_depth = 0;

        // Begin filling encoder options
        let rate_ctrl_mode = match props.control_rate {
            1 => "CBR",
            2 => "VBR",
            3 => "LOW_LATENCY",
            _ => "CONST_QP",
        };

        let framerate = format!("{}/{}", props.framerate.numerator, props.framerate.denominator);

        let slice_qp = if props.slice_qp == -1 {
            "AUTO".to_string()
        } else {
            format!("{}", props.slice_qp)
        };

        let gop_ctrl_mode = match props.gop_mode {
            1 => "PYRAMIDAL_GOP",
            2 => "LOW_DELAY_P",
            3 => "LOW_DELAY_B",
            _ => "DEFAULT_GOP",
        };

        let gdr_mode = match props.gdr_mode {
            1 => "GDR_VERTICAL",
            2 => "GDR_HORIZONTAL",
            _ => "DISABLE",
        };

        let profile = match props.codec_id {
            // h264
            CODEC_ID_H264 => match props.profile {
                ENC_H264_BASELINE => "AVC_BASELINE",
                ENC_H264_MAIN => "AVC_MAIN",
                ENC_H264_HIGH => "AVC_HIGH",
                other => {
                    return Err(Error::InvalidCodecProfile {
                        plugin: XrmPlugin::Encoder,
                        number: other,
                        codec: XrmCodec::H264,
                    })
                }
            },
            // h265
            CODEC_ID_HEVC => match props.profile {
                ENC_HEVC_MAIN => "HEVC_MAIN",
                ENC_HEVC_MAIN_INTRA => "HEVC_MAIN_INTRA",
                other => {
                    return Err(Error::InvalidCodecProfile {
                        plugin: XrmPlugin::Encoder,
                        number: other,
                        codec: XrmCodec::Hevc,
                    })
                }
            },
            other => {
                return Err(Error::InvalidCodecId {
                    plugin: XrmPlugin::Encoder,
                    codec: other,
                })
            }
        };

        let mut is_level_found = true;
        let mut level = match props.level {
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
            match props.codec_id {
                CODEC_ID_H264 => {
                    level = match props.level {
                        11 => "1.1",
                        12 => "1.2",
                        13 => "1.3",
                        22 => "2.2",
                        32 => "3.2",
                        42 => "4.2",
                        52 => "5.2",
                        other => {
                            return Err(Error::InvalidCodecLevel {
                                plugin: XrmPlugin::Encoder,
                                level: other,
                                codec: XrmCodec::H264,
                            })
                        }
                    }
                }
                CODEC_ID_HEVC => {
                    return Err(Error::InvalidCodecLevel {
                        plugin: XrmPlugin::Encoder,
                        level: props.level,
                        codec: XrmCodec::Hevc,
                    })
                }
                _ => {}
            }
        }

        let tier = if props.tier == 1 { "HIGH_TIER" } else { "MAIN_TIER" };

        let mut qp_ctrl_mode = match props.qp_mode {
            1 => "UNIFORM_QP",
            2 => "AUTO_QP",
            _ => "LOAD_QP | RELATIVE_QP",
        };

        let dependent_slice = if props.dependent_slice { "TRUE" } else { "FALSE" };

        let filler_data = if props.filler_data { "ENABLE" } else { "DISABLE" };

        let aspect_ratio = match props.aspect_ratio {
            XlnxAspectRatio::AspectRatio4x3 => "ASPECT_RATIO_4_3",
            XlnxAspectRatio::AspectRatio16x9 => "ASPECT_RATIO_16_9",
            XlnxAspectRatio::AspectRatioNone => "ASPECT_RATIO_NONE",
            XlnxAspectRatio::AspectRatioAuto => "ASPECT_RATIO_AUTO",
        };

        let color_space = "COLOUR_DESC_UNSPECIFIED";

        let mut scaling_list = if props.scaling_list == 0 { "FLAT" } else { "DEFAULT" };

        let loop_filter = if props.loop_filter { "ENABLE" } else { "DISABLE" };

        let entropy_mode = if props.entropy_mode == 0 { "MODE_CAVLC" } else { "MODE_CABAC" };

        let const_intra_pred = if props.constrained_intra_pred { "ENABLE" } else { "DISABLE" };

        let lambda_ctrl_mode = "DEFAULT_LDA";

        let prefetch_buffer = if props.prefetch_buffer { "ENABLE" } else { "DISABLE" };

        if props.tune_metrics {
            scaling_list = "FLAT";
            qp_ctrl_mode = "UNIFORM_QP";
        }

        let width = props.width;
        let height = props.height;
        let bit_rate = props.bit_rate;
        let max_bitrate = props.max_bitrate;
        let max_qp = props.max_qp;
        let min_qp = props.min_qp;
        let cpb_size = props.cpb_size;
        let initial_delay = props.initial_delay;
        let gop_size = props.gop_size;
        let num_bframes = props.num_bframes;
        let idr_period = props.idr_period;
        let num_slices = props.num_slices;
        let slice_size = props.slice_size;
        let num_cores = props.num_cores;

        let null = "\0";
        let enc_options = if props.codec_id == CODEC_ID_HEVC {
            format!(
                r#"[INPUT]
                Width = {width}
                Height = {height}
                [RATE_CONTROL]
                RateCtrlMode = {rate_ctrl_mode}
                FrameRate = {framerate}
                BitRate = {bit_rate}
                MaxBitRate = {max_bitrate}
                SliceQP = {slice_qp}
                MaxQP = {max_qp}
                MinQP = {min_qp}
                CPBSize = {cpb_size}
                InitialDelay = {initial_delay}
                [GOP]
                GopCtrlMode = {gop_ctrl_mode}
                Gop.GdrMode = {gdr_mode}
                Gop.Length = {gop_size}
                Gop.NumB = {num_bframes}
                Gop.FreqIDR = {idr_period}
                [SETTINGS]
                Profile = {profile}
                Level = {level}
                Tier = {tier}
                ChromaMode = CHROMA_4_2_0
                BitDepth = 8
                NumSlices = {num_slices}
                QPCtrlMode = {qp_ctrl_mode}
                SliceSize = {slice_size}
                DependentSlice = {dependent_slice}
                EnableFillerData = {filler_data}
                AspectRatio = {aspect_ratio}
                ColourDescription = {color_space}
                ScalingList = {scaling_list}
                LoopFilter = {loop_filter}
                ConstrainedIntraPred = {const_intra_pred}
                LambdaCtrlMode = {lambda_ctrl_mode}
                CacheLevel2 = {prefetch_buffer}
                NumCore = {num_cores}
                {null}"#
            )
        } else {
            format!(
                r#"[INPUT]
                Width = {width}
                Height = {height}
                [RATE_CONTROL]
                RateCtrlMode = {rate_ctrl_mode}
                FrameRate = {framerate}
                BitRate = {bit_rate}
                MaxBitRate = {max_bitrate}
                SliceQP = {slice_qp}
                MaxQP = {max_qp}
                MinQP = {min_qp}
                CPBSize = {cpb_size}
                InitialDelay = {initial_delay}
                [GOP]
                GopCtrlMode = {gop_ctrl_mode}
                Gop.GdrMode = {gdr_mode}
                Gop.Length = {gop_size}
                Gop.NumB = {num_bframes}
                Gop.FreqIDR = {idr_period}
                [SETTINGS]
                Profile = {profile}
                Level = {level}
                ChromaMode = CHROMA_4_2_0
                BitDepth = 8
                NumSlices = {num_slices}
                QPCtrlMode = {qp_ctrl_mode}
                SliceSize = {slice_size}
                EnableFillerData = {filler_data}
                AspectRatio = {aspect_ratio}
                ColourDescription = {color_space}
                ScalingList = {scaling_list}
                EntropyMode = {entropy_mode}
                LoopFilter = {loop_filter}
                ConstrainedIntraPred = {const_intra_pred}
                LambdaCtrlMode = {lambda_ctrl_mode}
                CacheLevel2 = {prefetch_buffer}
                NumCore = {num_cores}
                {null}"#
            )
        };
        let enc_options_ptr = Box::new(enc_options.as_ptr());

        Self::fill_params(&mut props, &mut params, &enc_options, enc_options_ptr.as_ref() as _);

        Ok(Self {
            inner,
            _params: params,
            _props: props,
            _enc_options: enc_options,
            _enc_options_ptr: enc_options_ptr,
        })
    }
}

impl AsRef<XmaEncoderProperties> for XlnxXmaEncoderProperties {
    fn as_ref(&self) -> &XmaEncoderProperties {
        &self.inner
    }
}

impl AsMut<XmaEncoderProperties> for XlnxXmaEncoderProperties {
    fn as_mut(&mut self) -> &mut XmaEncoderProperties {
        &mut self.inner
    }
}

impl XlnxXmaEncoderProperties {
    fn fill_params(
        props: &XlnxEncoderProperties,
        enc_params: &mut Box<[XmaParameter; MAX_ENC_PARAMS]>,
        enc_options: &str,
        enc_options_ptr_ptr: *const *const u8,
    ) {
        enc_params[0].name = ENC_OPTIONS_PARAM_NAME.as_ptr() as *mut i8;
        enc_params[0].type_ = XmaDataType_XMA_STRING;
        enc_params[0].length = enc_options.len();
        enc_params[0].value = enc_options_ptr_ptr as *mut std::ffi::c_void;

        enc_params[1].name = LATENCY_LOGGING_PARAM_NAME.as_ptr() as *mut i8;
        enc_params[1].type_ = XmaDataType_XMA_UINT32;
        enc_params[1].length = 4;
        enc_params[1].value = &props.latency_logging as *const _ as *mut std::ffi::c_void;

        enc_params[2].name = ENABLE_HW_IN_BUF_PARAM_NAME.as_ptr() as *mut i8;
        enc_params[2].type_ = XmaDataType_XMA_UINT32;
        enc_params[2].length = 4;
        enc_params[2].value = &props.enable_hw_buf as *const _ as *mut std::ffi::c_void;
    }
}
