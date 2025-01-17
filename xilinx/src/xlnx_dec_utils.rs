use std::{ffi::CString, os::raw::c_char, str::from_utf8};

use libloading::{Library, Symbol};

use crate::{strcpy_to_arr_i8, sys::*, xrm_precision_1000000_bitmask, Error, XlnxError, XrmContext, XrmPlugin};

const DEC_PLUGIN_NAME: &[u8] = b"xrmU30DecPlugin\0";

#[derive(Clone, Debug)]
pub struct XlnxDecBuffer<'a> {
    pub data: &'a [u8],
    pub size: usize,
    pub allocated: usize,
}

pub(crate) struct XlnxDecoderXrmCtx<'a> {
    pub xrm_reserve_id: Option<u64>,
    pub device_id: Option<u32>,
    pub dec_load: i32,
    pub(crate) decode_res_in_use: bool,
    pub(crate) xrm_ctx: &'a XrmContext,
    pub(crate) cu_list_res: Box<xrmCuListResourceV2>,
}

impl<'a> XlnxDecoderXrmCtx<'a> {
    pub fn new(xrm_ctx: &'a XrmContext, device_id: Option<u32>, reserve_id: Option<u64>, dec_load: i32) -> Self {
        Self {
            xrm_reserve_id: reserve_id,
            device_id,
            dec_load,
            decode_res_in_use: false,
            xrm_ctx,
            cu_list_res: Box::new(Default::default()),
        }
    }
}

/// Calculates the decoder load uing the xrmU30Dec plugin.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub fn xlnx_calc_dec_load(xrm_ctx: &XrmContext, xma_dec_props: *mut XmaDecoderProperties) -> Result<i32, Error> {
    let func_id = 0;
    let mut plugin_param: xrmPluginFuncParam = Default::default();
    unsafe {
        let lib = Library::new("/opt/xilinx/xrm/plugin/libxmaPropsTOjson.so")?;
        let convert_xma_props_to_json =
            lib.get::<Symbol<extern "C" fn(props: *mut XmaDecoderProperties, function: *const c_char, json_params: *mut c_char)>>(b"convertXmaPropsToJson")?;
        let function = CString::new("DECODER").unwrap();
        convert_xma_props_to_json(xma_dec_props, function.as_ptr(), plugin_param.input.as_mut_ptr());
    }

    unsafe {
        let ret = xrmExecPluginFunc(xrm_ctx.raw(), DEC_PLUGIN_NAME.as_ptr() as *mut i8, func_id, &mut plugin_param);
        if ret != XRM_SUCCESS as i32 {
            return Err(XlnxError::new(ret, Some("XRM decoder plugin failed to calculate decoder load.".to_string())).into());
        }
    }
    // parse the load from the output buffer of plugin param.
    let output_bytes = &plugin_param.output.map(|i| i as u8);
    let dec_plugin_output = from_utf8(output_bytes)
        .unwrap_or("-1")
        .split(' ')
        .next()
        .expect("split should emit at least one item");
    let load = dec_plugin_output.parse::<i32>().unwrap_or(-1);

    if load == -1 {
        return Err(Error::MalformedPluginResponse { plugin: XrmPlugin::Decoder });
    }

    Ok(load)
}

fn xlnx_fill_dec_pool_props(cu_pool_prop: &mut xrmCuPoolPropertyV2, dec_load: i32, device_id: Option<u32>) -> Result<(), Error> {
    cu_pool_prop.cuListNum = 1;

    let mut device_info = 0;

    if let Some(device_id) = device_id {
        device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) as u64
            | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
    }

    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[0].kernelName, "decoder")?;
    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[0].kernelAlias, "DECODER_MPSOC")?;
    cu_pool_prop.cuListProp.cuProps[0].devExcl = false;
    cu_pool_prop.cuListProp.cuProps[0].requestLoad = xrm_precision_1000000_bitmask(dec_load);
    cu_pool_prop.cuListProp.cuProps[0].deviceInfo = device_info;

    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[1].kernelName, "kernel_vcu_decoder")?;

    cu_pool_prop.cuListProp.cuProps[1].devExcl = false;
    cu_pool_prop.cuListProp.cuProps[1].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
    cu_pool_prop.cuListProp.cuProps[1].deviceInfo = device_info;
    // we defined 2 cu requests to the properties.
    cu_pool_prop.cuListProp.cuNum = 2;

    Ok(())
}

pub(crate) fn xlnx_reserve_dec_resource(xlnx_dec_ctx: &mut XlnxDecoderXrmCtx<'_>) -> Result<Box<xrmCuPoolResInforV2>, Error> {
    let mut cu_pool_prop: Box<xrmCuPoolPropertyV2> = Box::new(Default::default());
    let mut cu_pool_res_infor: Box<xrmCuPoolResInforV2> = Box::new(Default::default());
    xlnx_fill_dec_pool_props(&mut cu_pool_prop, xlnx_dec_ctx.dec_load, xlnx_dec_ctx.device_id)?;

    unsafe {
        // If this context was used for an allocation previously, release it now.
        // It's expected that this is dead code, but if it does somehow get run
        // it would prevent a leak of the CU pool.
        if let Some(xrm_reserve_id) = xlnx_dec_ctx.xrm_reserve_id {
            if xrmCuPoolRelinquishV2(xlnx_dec_ctx.xrm_ctx.raw(), xrm_reserve_id) {
                xlnx_dec_ctx.xrm_reserve_id = None;
            } else {
                return Err(Error::FailedToReleaseCuPool { plugin: XrmPlugin::Decoder });
            }
        }

        let num_cu_pool = xrmCheckCuPoolAvailableNumV2(xlnx_dec_ctx.xrm_ctx.raw(), cu_pool_prop.as_mut());
        if num_cu_pool <= 0 {
            return Err(Error::ReserveCuPoolError { plugin: XrmPlugin::Decoder });
        }

        let xrm_reserve_id = xrmCuPoolReserveV2(xlnx_dec_ctx.xrm_ctx.raw(), cu_pool_prop.as_mut(), cu_pool_res_infor.as_mut());
        if xrm_reserve_id == 0 {
            return Err(Error::ReserveCuPoolError { plugin: XrmPlugin::Decoder });
        }
        xlnx_dec_ctx.xrm_reserve_id = Some(xrm_reserve_id);
    }

    Ok(cu_pool_res_infor)
}

/// Allocates decoder CU
fn xlnx_dec_cu_alloc(xma_dec_props: &mut XmaDecoderProperties, xlnx_dec_ctx: &mut XlnxDecoderXrmCtx<'_>) -> Result<(), Error> {
    // Allocate xrm decoder
    let mut decode_cu_list_prop = Box::new(xrmCuListPropertyV2 {
        cuNum: 2,
        ..Default::default()
    });

    strcpy_to_arr_i8(&mut decode_cu_list_prop.cuProps[0].kernelName, "decoder")?;
    strcpy_to_arr_i8(&mut decode_cu_list_prop.cuProps[0].kernelAlias, "DECODER_MPSOC")?;
    decode_cu_list_prop.cuProps[0].devExcl = false;
    decode_cu_list_prop.cuProps[0].requestLoad = xrm_precision_1000000_bitmask(xlnx_dec_ctx.dec_load);

    strcpy_to_arr_i8(&mut decode_cu_list_prop.cuProps[1].kernelName, "kernel_vcu_decoder")?;
    decode_cu_list_prop.cuProps[1].devExcl = false;
    decode_cu_list_prop.cuProps[1].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);

    match (xlnx_dec_ctx.device_id, xlnx_dec_ctx.xrm_reserve_id) {
        (Some(device_id), Some(xrm_reserve_id)) => {
            let device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) as u64
                | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
            decode_cu_list_prop.cuProps[0].deviceInfo = device_info;
            decode_cu_list_prop.cuProps[0].poolId = xrm_reserve_id;
            decode_cu_list_prop.cuProps[1].deviceInfo = device_info;
            decode_cu_list_prop.cuProps[1].poolId = xrm_reserve_id;
        }
        (Some(device_id), None) => {
            let device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) as u64
                | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
            decode_cu_list_prop.cuProps[0].deviceInfo = device_info;
            decode_cu_list_prop.cuProps[1].deviceInfo = device_info;
        }
        (None, Some(reserve_id)) => {
            decode_cu_list_prop.cuProps[0].poolId = reserve_id;
            decode_cu_list_prop.cuProps[1].poolId = reserve_id;
        }
        (None, None) => {
            return Err(Error::ReserveCuPoolError { plugin: XrmPlugin::Scaler });
        }
    }

    // If this context was used for a reservation previously, release it now.
    // This is almost certainly dead code but if it does somehow get run
    // it would prevent a leak of CU resources.
    if xlnx_dec_ctx.decode_res_in_use {
        if unsafe { xrmCuListReleaseV2(xlnx_dec_ctx.xrm_ctx.raw(), xlnx_dec_ctx.cu_list_res.as_mut()) } {
            xlnx_dec_ctx.decode_res_in_use = false;
        } else {
            return Err(Error::FailedToReleaseCu { plugin: XrmPlugin::Decoder });
        }
    }

    // Now make the new reservation
    let ret = unsafe { xrmCuListAllocV2(xlnx_dec_ctx.xrm_ctx.raw(), decode_cu_list_prop.as_mut(), xlnx_dec_ctx.cu_list_res.as_mut()) };
    if ret != XRM_SUCCESS as _ {
        return Err(Error::ReserveCuError {
            plugin: XrmPlugin::Decoder,
            reserve_id: xlnx_dec_ctx.xrm_reserve_id,
            device_id: xlnx_dec_ctx.device_id,
            xlnx_error_code: ret,
        });
    }

    xlnx_dec_ctx.decode_res_in_use = true;

    // Set XMA plugin shared object and device index.
    xma_dec_props.plugin_lib = xlnx_dec_ctx.cu_list_res.cuResources[0].kernelPluginFileName.as_mut_ptr();
    xma_dec_props.dev_index = xlnx_dec_ctx.cu_list_res.cuResources[0].deviceId;

    // Select ddr bank based on xclbin metadata.
    xma_dec_props.ddr_bank_index = -1;
    xma_dec_props.cu_index = xlnx_dec_ctx.cu_list_res.cuResources[1].cuId;
    xma_dec_props.channel_id = xlnx_dec_ctx.cu_list_res.cuResources[1].channelId;

    Ok(())
}

/// Attempts to create decoder session
pub(crate) fn xlnx_create_dec_session(
    xma_dec_props: &mut XmaDecoderProperties,
    xlnx_dec_ctx: &mut XlnxDecoderXrmCtx<'_>,
) -> Result<*mut XmaDecoderSession, Error> {
    xlnx_dec_cu_alloc(xma_dec_props, xlnx_dec_ctx)?;

    let dec_session = unsafe { xma_dec_session_create(xma_dec_props) };
    if dec_session.is_null() {
        return Err(Error::SessionCreateFailed { plugin: XrmPlugin::Decoder });
    }

    Ok(dec_session)
}

impl<'a> Drop for XlnxDecoderXrmCtx<'a> {
    fn drop(&mut self) {
        unsafe {
            if self.xrm_ctx.raw().is_null() {
                return;
            }
            if self.decode_res_in_use {
                let _ = xrmCuListReleaseV2(self.xrm_ctx.raw(), self.cu_list_res.as_mut());
            }
            if let Some(xrm_reserve_id) = self.xrm_reserve_id {
                let _ = xrmCuPoolRelinquishV2(self.xrm_ctx.raw(), xrm_reserve_id);
            }
        }
    }
}
