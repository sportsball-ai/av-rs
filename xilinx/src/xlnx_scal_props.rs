use crate::{strcpy_to_arr_i8, sys::*, xlnx_scaler::SCAL_MAX_ABR_CHANNELS};
use std::ptr;

pub const MAX_SCAL_PARAMS: usize = 4;
const ENABLE_PIPELINE_PARAM_NAME: &[u8] = b"enable_pipeline\0";
const LOG_LEVEL_PARAM_NAME: &[u8] = b"logLevel\0";
const MIX_RATE_PARAM_NAME: &[u8] = b"MixRate\0";
const LATENCY_LOGGING_PARAM_NAME: &[u8] = b"latency_logging\0";
const TRANSCODE_WIDTH_ALIGN: i32 = 256;

pub struct XlnxScalerProperties {
    pub in_width: i32,
    pub in_height: i32,
    pub framerate: XmaFraction,
    pub nb_outputs: i32,
    pub out_width: [i32; SCAL_MAX_ABR_CHANNELS],
    pub out_height: [i32; SCAL_MAX_ABR_CHANNELS],
    pub enable_pipeline: u32,
    pub log_level: i32,
    pub latency_logging: i32,
}

pub struct XlnxXmaScalerProperties {
    // XXX: The drop order of these fields matters!
    inner: XmaScalerProperties,
    // `inner` references `_params`.
    _params: Box<[XmaParameter; MAX_SCAL_PARAMS]>,
    // `_params` references `_props`.
    _props: Box<XlnxScalerProperties>,
}

impl From<XlnxScalerProperties> for XlnxXmaScalerProperties {
    fn from(props: XlnxScalerProperties) -> Self {
        let mut props = Box::new(props);
        let mut params: Box<[XmaParameter; MAX_SCAL_PARAMS]> = Default::default();
        Self::fill_params(&mut props, &mut params);

        let mut inner = XmaScalerProperties::default();
        inner.hwscaler_type = XmaScalerType_XMA_POLYPHASE_SCALER_TYPE;
        strcpy_to_arr_i8(&mut inner.hwvendor_string, "Xilinx").expect("Xilinx should definitely fit");
        inner.num_outputs = props.nb_outputs;
        inner.input.format = XmaFormatType_XMA_VCU_NV12_FMT_TYPE;
        inner.input.width = props.in_width;
        inner.input.height = props.in_height;
        inner.input.stride = Self::align(props.in_width, TRANSCODE_WIDTH_ALIGN);
        inner.input.framerate.numerator = props.framerate.numerator;
        inner.input.framerate.denominator = props.framerate.denominator;

        for i in 0..props.nb_outputs as usize {
            inner.output[i].format = XmaFormatType_XMA_VCU_NV12_FMT_TYPE;
            inner.output[i].bits_per_pixel = 8;
            inner.output[i].width = props.out_width[i];
            inner.output[i].height = props.out_height[i];
            inner.output[i].stride = Self::align(props.out_width[i], TRANSCODE_WIDTH_ALIGN);
            inner.output[i].coeffLoad = 0;
            inner.output[i].framerate.numerator = props.framerate.numerator;
            inner.output[i].framerate.denominator = props.framerate.denominator;
        }

        Self {
            inner,
            _params: params,
            _props: props,
        }
    }
}

impl AsRef<XmaScalerProperties> for XlnxXmaScalerProperties {
    fn as_ref(&self) -> &XmaScalerProperties {
        &self.inner
    }
}

impl AsMut<XmaScalerProperties> for XlnxXmaScalerProperties {
    fn as_mut(&mut self) -> &mut XmaScalerProperties {
        &mut self.inner
    }
}

impl XlnxXmaScalerProperties {
    fn fill_params(scal_props: &XlnxScalerProperties, scal_params: &mut Box<[XmaParameter; MAX_SCAL_PARAMS]>) {
        scal_params[0].name = ENABLE_PIPELINE_PARAM_NAME.as_ptr() as *mut i8;
        scal_params[0].type_ = XmaDataType_XMA_UINT32;
        scal_params[0].length = 4;
        scal_params[0].value = &scal_props.enable_pipeline as *const _ as *mut std::ffi::c_void;

        scal_params[1].name = LOG_LEVEL_PARAM_NAME.as_ptr() as *mut i8;
        scal_params[1].type_ = XmaDataType_XMA_UINT32;
        scal_params[1].length = 4;
        scal_params[1].value = &scal_props.log_level as *const _ as *mut std::ffi::c_void;

        scal_params[2].name = MIX_RATE_PARAM_NAME.as_ptr() as *mut i8;
        scal_params[2].type_ = XmaDataType_XMA_UINT64;
        scal_params[2].length = 8;
        scal_params[2].value = ptr::null_mut();

        scal_params[3].name = LATENCY_LOGGING_PARAM_NAME.as_ptr() as *mut i8;
        scal_params[3].type_ = XmaDataType_XMA_UINT32;
        scal_params[3].length = 4;
        scal_params[3].value = &scal_props.latency_logging as *const _ as *mut std::ffi::c_void;
    }

    fn align(x: i32, align: i32) -> i32 {
        ((x) + (align) - 1) & !((align) - 1)
    }
}
