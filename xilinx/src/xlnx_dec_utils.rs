use std::{ffi::CString, os::raw::c_char, str::from_utf8};

use libloading::{Library, Symbol};
use simple_error::{bail, SimpleError};

use crate::{strcpy_to_arr_i8, sys::*, xrm_precision_1000000_bitmask};

#[derive(Clone, Debug)]
pub struct XlnxDecBuffer<'a> {
    pub data: &'a [u8],
    pub size: usize,
    pub allocated: usize,
}

pub struct XlnxDecoderXrmCtx {
    pub xrm_reserve_id: u64,
    pub device_id: i32,
    pub dec_load: i32,
    pub decode_res_in_use: bool,
    pub xrm_ctx: xrmContext,
    pub cu_list_res: xrmCuListResource,
}

/// Calculates the decoder load uing the xrmU30Dec plugin.
pub fn xlnx_calc_dec_load(xrm_ctx: xrmContext, xma_dec_props: *mut XmaDecoderProperties) -> Result<i32, SimpleError> {
    let func_id = 0;
    let mut plugin_param: xrmPluginFuncParam = Default::default();
    unsafe {
        match Library::new("/opt/xilinx/xrm/plugin/libxmaPropsTOjson.so") {
            Ok(lib) => {
                match lib
                    .get::<Symbol<extern "C" fn(props: *mut XmaDecoderProperties, function: *const c_char, json_params: *mut c_char)>>(b"convertXmaPropsToJson")
                {
                    Ok(convert_xma_props_to_json) => {
                        let function = CString::new("DECODER").unwrap();
                        convert_xma_props_to_json(xma_dec_props, function.as_ptr(), plugin_param.input.as_mut_ptr());
                    }
                    Err(e) => bail!("{}", e),
                }
            }
            Err(e) => bail!("{}", e),
        };
    }

    let plugin_name = CString::new("xrmU30DecPlugin").unwrap().into_raw();

    unsafe {
        let ret = xrmExecPluginFunc(xrm_ctx, plugin_name, func_id, &mut plugin_param);
        if ret != XRM_SUCCESS as i32 {
            bail!("XRM decoder plugin failed to calculate decoder load. error: {}", ret);
        }
    }
    // parse the load from the output buffer of plugin param.
    let output_bytes = &plugin_param.output.map(|i| i as u8);
    let dec_plugin_output = from_utf8(output_bytes).unwrap_or("-1").split(" ").collect::<Vec<&str>>()[0];
    let load = dec_plugin_output.parse::<i32>().unwrap_or(-1);

    if load == -1 {
        bail!("Unable to parse load calculation from XRM decoder plugin");
    }

    Ok(load)
}

pub fn xlnx_fill_dec_pool_props(cu_pool_prop: &mut xrmCuPoolProperty, dec_load: i32) -> Result<(), SimpleError> {
    cu_pool_prop.cuListProp.sameDevice = true;
    cu_pool_prop.cuListNum = 1;
    let mut buff_len = cu_pool_prop.cuListProp.cuProps[0].kernelName.len();
    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[0].kernelName, buff_len, "decoder")?;
    buff_len = cu_pool_prop.cuListProp.cuProps[0].kernelAlias.len();
    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[0].kernelAlias, buff_len, "DECODER_MPSOC")?;
    cu_pool_prop.cuListProp.cuProps[0].devExcl = false;
    cu_pool_prop.cuListProp.cuProps[0].requestLoad = xrm_precision_1000000_bitmask(dec_load);

    buff_len = cu_pool_prop.cuListProp.cuProps[0].kernelName.len();
    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[1].kernelName, buff_len, "kernel_vcu_decoder")?;
    buff_len = cu_pool_prop.cuListProp.cuProps[0].kernelAlias.len();
    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[1].kernelAlias, buff_len, "")?;
    cu_pool_prop.cuListProp.cuProps[1].devExcl = false;
    cu_pool_prop.cuListProp.cuProps[1].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
    // we defined 2 cu requests to the properties.
    cu_pool_prop.cuListProp.cuNum = 2;

    Ok(())
}

pub fn xlnx_reserve_dec_resource(xlnx_dec_ctx: &mut XlnxDecoderXrmCtx) -> Result<(), SimpleError> {
    // a device has already been chosen, there is no need to assign a reserve id.
    if xlnx_dec_ctx.device_id >= 0 {
        return Ok(());
    }

    let mut cu_pool_prop: xrmCuPoolProperty = Default::default();
    xlnx_fill_dec_pool_props(&mut cu_pool_prop, xlnx_dec_ctx.dec_load)?;

    unsafe {
        let num_cu_pool = xrmCheckCuPoolAvailableNum(xlnx_dec_ctx.xrm_ctx, &mut cu_pool_prop);
        if num_cu_pool <= 0 {
            bail!("No resources avaliable for allocation")
        }

        xlnx_dec_ctx.xrm_reserve_id = xrmCuPoolReserve(xlnx_dec_ctx.xrm_ctx, &mut cu_pool_prop);
        if xlnx_dec_ctx.xrm_reserve_id == 0 {
            bail!("failed to reserve decode cu pool")
        }
    }

    Ok(())
}

/// Allocates decoder CU based on device_id
pub fn xlnx_dec_cu_alloc_device_id(xma_dec_props: &mut XmaDecoderProperties, xlnx_dec_ctx: &mut XlnxDecoderXrmCtx) -> Result<(), SimpleError> {
    let mut decode_cu_hw_prop: xrmCuProperty = Default::default();
    let mut decode_cu_sw_prop: xrmCuProperty = Default::default();

    let mut buff_len = decode_cu_hw_prop.kernelName.len();
    strcpy_to_arr_i8(&mut decode_cu_hw_prop.kernelName, buff_len, "decoder")?;
    buff_len = decode_cu_hw_prop.kernelAlias.len();
    strcpy_to_arr_i8(&mut decode_cu_hw_prop.kernelAlias, buff_len, "DECODER_MPSOC")?;
    decode_cu_hw_prop.devExcl = false;
    decode_cu_hw_prop.requestLoad = xrm_precision_1000000_bitmask(xlnx_dec_ctx.dec_load);

    decode_cu_sw_prop.kernelName.len();
    strcpy_to_arr_i8(&mut decode_cu_sw_prop.kernelName, buff_len, "kernel_vcu_decoder")?;
    decode_cu_sw_prop.devExcl = false;
    decode_cu_sw_prop.requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);

    let ret = unsafe {
        xrmCuAllocFromDev(
            xlnx_dec_ctx.xrm_ctx,
            xlnx_dec_ctx.device_id,
            &mut decode_cu_hw_prop,
            &mut xlnx_dec_ctx.cu_list_res.cuResources[0],
        )
    };
    if ret <= XRM_ERROR {
        bail!("xrm failed to allocate decoder resources on device: {}", xlnx_dec_ctx.device_id);
    }

    let ret = unsafe {
        xrmCuAllocFromDev(
            xlnx_dec_ctx.xrm_ctx,
            xlnx_dec_ctx.device_id,
            &mut decode_cu_sw_prop,
            &mut xlnx_dec_ctx.cu_list_res.cuResources[1],
        )
    };
    if ret <= XRM_ERROR {
        bail!("xrm failed to allocate decoder resources on device: {}", xlnx_dec_ctx.device_id);
    }

    // Set XMA plugin shared object and device index.
    xma_dec_props.plugin_lib = xlnx_dec_ctx.cu_list_res.cuResources[0].kernelPluginFileName.as_mut_ptr();
    xma_dec_props.dev_index = xlnx_dec_ctx.cu_list_res.cuResources[0].deviceId;

    // Select ddr bank based on xclbin metadata.
    xma_dec_props.ddr_bank_index = -1;
    xma_dec_props.cu_index = xlnx_dec_ctx.cu_list_res.cuResources[1].cuId;
    xma_dec_props.channel_id = xlnx_dec_ctx.cu_list_res.cuResources[1].channelId;

    Ok(())
}

/// Allocates decoder CU based on reserve_id
pub fn xlnx_dec_cu_alloc_reserve_id(xma_dec_props: &mut XmaDecoderProperties, xlnx_dec_ctx: &mut XlnxDecoderXrmCtx) -> Result<(), SimpleError> {
    // Allocate xrm decoder
    let mut decode_cu_list_prop: xrmCuListProperty = Default::default();
    decode_cu_list_prop.cuNum = 2;
    let mut buff_len = decode_cu_list_prop.cuProps[0].kernelName.len();
    strcpy_to_arr_i8(&mut decode_cu_list_prop.cuProps[0].kernelName, buff_len, "decoder")?;
    buff_len = decode_cu_list_prop.cuProps[0].kernelAlias.len();
    strcpy_to_arr_i8(&mut decode_cu_list_prop.cuProps[0].kernelAlias, buff_len, "DECODER_MPSOC")?;
    decode_cu_list_prop.cuProps[0].devExcl = false;
    decode_cu_list_prop.cuProps[0].requestLoad = xrm_precision_1000000_bitmask(xlnx_dec_ctx.dec_load);
    decode_cu_list_prop.cuProps[0].poolId = xlnx_dec_ctx.xrm_reserve_id;

    buff_len = decode_cu_list_prop.cuProps[1].kernelName.len();
    strcpy_to_arr_i8(&mut decode_cu_list_prop.cuProps[1].kernelName, buff_len, "kernel_vcu_decoder")?;
    buff_len = decode_cu_list_prop.cuProps[1].kernelAlias.len();
    strcpy_to_arr_i8(&mut decode_cu_list_prop.cuProps[1].kernelAlias, buff_len, "")?;
    decode_cu_list_prop.cuProps[1].devExcl = false;
    decode_cu_list_prop.cuProps[1].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
    decode_cu_list_prop.cuProps[1].poolId = xlnx_dec_ctx.xrm_reserve_id;

    if unsafe { xrmCuListAlloc(xlnx_dec_ctx.xrm_ctx, &mut decode_cu_list_prop, &mut xlnx_dec_ctx.cu_list_res) } != 0 {
        bail!("Failed to allocate cu list from reserve id {}", xlnx_dec_ctx.xrm_reserve_id)
    }

    // Set XMA plugin shared object and device index.
    xma_dec_props.plugin_lib = xlnx_dec_ctx.cu_list_res.cuResources[0].kernelPluginFileName.as_mut_ptr();
    xma_dec_props.dev_index = xlnx_dec_ctx.cu_list_res.cuResources[0].deviceId;

    // Select ddr bank based on xclbin metadata.
    xma_dec_props.ddr_bank_index = -1;
    xma_dec_props.cu_index = xlnx_dec_ctx.cu_list_res.cuResources[1].cuId;
    xma_dec_props.channel_id = xlnx_dec_ctx.cu_list_res.cuResources[1].channelId;

    Ok(())
}

/// Allocates decoder CU
pub fn xlnx_dec_cu_alloc(xma_dec_props: &mut XmaDecoderProperties, xlnx_dec_ctx: &mut XlnxDecoderXrmCtx) -> Result<(), SimpleError> {
    if xlnx_dec_ctx.device_id >= 0 {
        xlnx_dec_cu_alloc_device_id(xma_dec_props, xlnx_dec_ctx)?;
    } else {
        xlnx_dec_cu_alloc_reserve_id(xma_dec_props, xlnx_dec_ctx)?;
    }

    Ok(())
}

/// Attempts to create decoder session
pub fn xlnx_create_dec_session(xma_dec_props: &mut XmaDecoderProperties, xlnx_dec_ctx: &mut XlnxDecoderXrmCtx) -> Result<*mut XmaDecoderSession, SimpleError> {
    xlnx_dec_cu_alloc(xma_dec_props, xlnx_dec_ctx)?;

    let dec_session = unsafe { xma_dec_session_create(xma_dec_props) };
    if dec_session.is_null() {
        bail!("Failed to create decoder session. Session is null")
    }

    Ok(dec_session)
}

impl Drop for XlnxDecoderXrmCtx {
    fn drop(&mut self) {
        if self.xrm_ctx.is_null() {
            return;
        }
        unsafe {
            if self.decode_res_in_use {
                xrmCuListRelease(self.xrm_ctx, &mut self.cu_list_res);
            }
            if self.xrm_reserve_id != 0 {
                xrmCuPoolRelinquish(self.xrm_ctx, self.xrm_reserve_id);
            }
            xrmDestroyContext(self.xrm_ctx);
        }
    }
}
