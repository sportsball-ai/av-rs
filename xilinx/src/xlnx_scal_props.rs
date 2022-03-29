use crate::{strcpy_to_arr_i8, sys::*, xlnx_scaler::SCAL_MAX_ABR_CHANNELS};
use simple_error::SimpleError;
use std::ptr;

pub(crate) const MAX_SCAL_PARAMS: usize = 4;
const ENABLE_PIPELINE_PARAM_NAME: &[u8] = b"enable_pipeline\0";
const LOG_LEVEL_PARAM_NAME: &[u8] = b"logLevel\0";
const MIX_RATE_PARAM_NAME: &[u8] = b"MixRate\0";
const LATENCY_LOGGING_PARAM_NAME: &[u8] = b"latency_logging\0";
const TRANSCODE_WIDTH_ALIGN: i32 = 256;

pub struct XlnxScalerProperties {
    pub in_width: i32,
    pub in_height: i32,
    pub fr_num: i32,
    pub fr_den: i32,
    pub nb_outputs: i32,
    pub out_width: [i32; SCAL_MAX_ABR_CHANNELS],
    pub out_height: [i32; SCAL_MAX_ABR_CHANNELS],
    pub enable_pipeline: u32,
    pub log_level: i32,
    pub latency_logging: i32,
}

fn xlnx_fill_scal_params(scal_props: &mut Box<XlnxScalerProperties>, scal_params: &mut Box<[XmaParameter; MAX_SCAL_PARAMS]>) {
    scal_params[0].name = ENABLE_PIPELINE_PARAM_NAME.as_ptr() as *mut i8;
    scal_params[0].type_ = XmaDataType_XMA_UINT32;
    scal_params[0].length = 4;
    scal_params[0].value = &mut scal_props.enable_pipeline as *mut _ as *mut std::ffi::c_void;

    scal_params[1].name = LOG_LEVEL_PARAM_NAME.as_ptr() as *mut i8;
    scal_params[1].type_ = XmaDataType_XMA_UINT32;
    scal_params[1].length = 4;
    scal_params[1].value = &mut scal_props.log_level as *mut _ as *mut std::ffi::c_void;

    scal_params[2].name = MIX_RATE_PARAM_NAME.as_ptr() as *mut i8;
    scal_params[2].type_ = XmaDataType_XMA_UINT64;
    scal_params[2].length = 8;
    scal_params[2].value = ptr::null_mut();

    scal_params[3].name = LATENCY_LOGGING_PARAM_NAME.as_ptr() as *mut i8;
    scal_params[3].type_ = XmaDataType_XMA_UINT32;
    scal_params[3].length = 4;
    scal_params[3].value = &mut scal_props.latency_logging as *mut _ as *mut std::ffi::c_void;
}

fn align(x: i32, align: i32) -> i32 {
    ((x) + (align) - 1) & !((align) - 1)
}

pub fn xlnx_create_xma_scal_props(
    scal_props: &mut Box<XlnxScalerProperties>,
    scal_params: &mut Box<[XmaParameter; MAX_SCAL_PARAMS]>,
) -> Result<Box<XmaScalerProperties>, SimpleError> {
    let xma_scal_props: XmaScalerProperties = Default::default();
    let mut xma_scal_props = Box::new(xma_scal_props);

    xma_scal_props.hwscaler_type = XmaScalerType_XMA_POLYPHASE_SCALER_TYPE;
    strcpy_to_arr_i8(&mut xma_scal_props.hwvendor_string, "Xilinx")?;
    xma_scal_props.num_outputs = scal_props.nb_outputs;
    xma_scal_props.input.format = XmaFormatType_XMA_VCU_NV12_FMT_TYPE;
    xma_scal_props.input.width = scal_props.in_width;
    xma_scal_props.input.height = scal_props.in_height;
    xma_scal_props.input.stride = align(scal_props.in_width, TRANSCODE_WIDTH_ALIGN);
    xma_scal_props.input.framerate.numerator = scal_props.fr_num;
    xma_scal_props.input.framerate.denominator = scal_props.fr_den;

    for i in 0..scal_props.nb_outputs as usize {
        xma_scal_props.output[i].format = XmaFormatType_XMA_VCU_NV12_FMT_TYPE;
        xma_scal_props.output[i].bits_per_pixel = 8;
        xma_scal_props.output[i].width = scal_props.out_width[i];
        xma_scal_props.output[i].height = scal_props.out_height[i];
        xma_scal_props.output[i].stride = align(scal_props.out_width[i], TRANSCODE_WIDTH_ALIGN);
        xma_scal_props.output[i].coeffLoad = 0;
        xma_scal_props.output[i].framerate.numerator = scal_props.fr_num;
        xma_scal_props.output[i].framerate.denominator = scal_props.fr_den;
    }

    xlnx_fill_scal_params(scal_props, scal_params);

    Ok(xma_scal_props)
}
