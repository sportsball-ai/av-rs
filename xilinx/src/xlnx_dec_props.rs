use simple_error::SimpleError;

use crate::{strcpy_to_arr_i8, sys::*};

const MAX_DEC_PARAMS: usize = 11;
const BIT_DEPTH_PARAM_NAME: &[u8] = b"bitdepth\0";
const CODEC_TYPE_PARAM_NAME: &[u8] = b"codec_type\0";
const LOW_LATENCY_PARAM_NAME: &[u8] = b"low_latency\0";
const ENTROPY_BUF_COUNT_PARAM_NAME: &[u8] = b"entropy_buffers_count\0";
const ZERO_COPY_PARAM_NAME: &[u8] = b"zero_copy\0";
const PROFILE_PARAM_NAME: &[u8] = b"profile\0";
const LEVEL_PARAM_NAME: &[u8] = b"level\0";
const CHROMA_MODE_PARAM_NAME: &[u8] = b"chroma_mode\0";
const SCAN_TYPE_PARAM_NAME: &[u8] = b"scan_type\0";
const LATENCY_LOGGING_PARAM_NAME: &[u8] = b"latency_logging\0";
const SPLITBUFF_MODE_PARAM_NAME: &[u8] = b"splitbuff_mode\0";

pub struct XlnxDecoderProperties {
    pub width: i32,
    pub height: i32,
    pub bitdepth: u32,
    pub codec_type: u32,
    pub low_latency: u32,
    pub entropy_buffers_count: u32,
    pub zero_copy: u32,
    pub profile: u32,
    pub level: u32,
    pub chroma_mode: u32,
    pub scan_type: u32,
    pub latency_logging: u32,
    pub splitbuff_mode: u32,
    pub framerate: XmaFraction,
}

pub fn xlnx_fill_dec_params(dec_props: &mut Box<XlnxDecoderProperties>, dec_params: &mut Box<[XmaParameter; MAX_DEC_PARAMS]>) {
    dec_params[0].name = BIT_DEPTH_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[0].type_ = XmaDataType_XMA_UINT32;
    dec_params[0].length = 4;
    dec_params[0].value = &mut dec_props.bitdepth as *mut _ as *mut std::ffi::c_void;

    dec_params[1].name = CODEC_TYPE_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[1].type_ = XmaDataType_XMA_UINT32;
    dec_params[1].length = 4;
    dec_params[1].value = &mut dec_props.codec_type as *mut _ as *mut std::ffi::c_void;

    dec_params[2].name = LOW_LATENCY_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[2].type_ = XmaDataType_XMA_UINT32;
    dec_params[2].length = 4;
    dec_params[2].value = &mut dec_props.low_latency as *mut _ as *mut std::ffi::c_void;

    dec_params[3].name = ENTROPY_BUF_COUNT_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[3].type_ = XmaDataType_XMA_UINT32;
    dec_params[3].length = 4;
    dec_params[3].value = &mut dec_props.entropy_buffers_count as *mut _ as *mut std::ffi::c_void;

    dec_params[4].name = ZERO_COPY_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[4].type_ = XmaDataType_XMA_UINT32;
    dec_params[4].length = 4;
    dec_params[4].value = &mut dec_props.zero_copy as *mut _ as *mut std::ffi::c_void;

    dec_params[5].name = PROFILE_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[5].type_ = XmaDataType_XMA_UINT32;
    dec_params[5].length = 4;
    dec_params[5].value = &mut dec_props.profile as *mut _ as *mut std::ffi::c_void;

    dec_params[6].name = LEVEL_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[6].type_ = XmaDataType_XMA_UINT32;
    dec_params[6].length = 4;
    dec_params[6].value = &mut dec_props.level as *mut _ as *mut std::ffi::c_void;

    dec_params[7].name = CHROMA_MODE_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[7].type_ = XmaDataType_XMA_UINT32;
    dec_params[7].length = 4;
    dec_params[7].value = &mut dec_props.chroma_mode as *mut _ as *mut std::ffi::c_void;

    dec_params[8].name = SCAN_TYPE_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[8].type_ = XmaDataType_XMA_UINT32;
    dec_params[8].length = 4;
    dec_params[8].value = &mut dec_props.scan_type as *mut _ as *mut std::ffi::c_void;

    dec_params[9].name = LATENCY_LOGGING_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[9].type_ = XmaDataType_XMA_UINT32;
    dec_params[9].length = 4;
    dec_params[9].value = &mut dec_props.latency_logging as *mut _ as *mut std::ffi::c_void;

    dec_params[10].name = SPLITBUFF_MODE_PARAM_NAME.as_ptr() as *mut i8;
    dec_params[10].type_ = XmaDataType_XMA_UINT32;
    dec_params[10].length = 4;
    dec_params[10].value = &mut dec_props.splitbuff_mode as *mut _ as *mut std::ffi::c_void;
}

pub fn xlnx_create_xma_dec_props(
    dec_props: &mut Box<XlnxDecoderProperties>,
    dec_params: &mut Box<[XmaParameter; MAX_DEC_PARAMS]>,
) -> Result<Box<XmaDecoderProperties>, SimpleError> {
    let xma_dec_props: XmaDecoderProperties = Default::default();
    let mut xma_dec_props = Box::new(xma_dec_props);
    // hwvendor_string needs to be MPSoC
    strcpy_to_arr_i8(&mut xma_dec_props.hwvendor_string, "MPSoC")?;
    xma_dec_props.hwdecoder_type = XmaDecoderType_XMA_MULTI_DECODER_TYPE;
    xma_dec_props.params = dec_params.as_mut_ptr();
    xma_dec_props.param_cnt = MAX_DEC_PARAMS as u32;
    xma_dec_props.width = dec_props.width;
    xma_dec_props.height = dec_props.height;
    xma_dec_props.bits_per_pixel = dec_props.bitdepth as i32;
    xma_dec_props.framerate = dec_props.framerate;

    xlnx_fill_dec_params(dec_props, dec_params);

    Ok(xma_dec_props)
}
