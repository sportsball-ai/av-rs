use crate::{strcpy_to_arr_i8, sys::*, xrm_precision_1000000_bitmask};
use libloading::{Library, Symbol};
use simple_error::{bail, SimpleError};
use std::{ffi::CString, os::raw::c_char, str::from_utf8};

pub struct XlnxEncoderXrmCtx {
    pub xrm_reserve_id: u64,
    pub device_id: i32,
    pub enc_load: i32,
    pub encode_res_in_use: bool,
    pub xrm_ctx: xrmContext,
    pub cu_list_res: xrmCuListResource,
}

/// Calculates the encoder load uing the xrmU30Enc plugin.
pub fn xlnx_calc_enc_load(xrm_ctx: xrmContext, xma_enc_props: *mut XmaEncoderProperties) -> Result<i32, SimpleError> {
    let func_id = 0;
    let mut plugin_param: xrmPluginFuncParam = Default::default();
    unsafe {
        match Library::new("/opt/xilinx/xrm/plugin/libxmaPropsTOjson.so") {
            Ok(lib) => {
                match lib
                    .get::<Symbol<extern "C" fn(props: *mut XmaEncoderProperties, function: *const c_char, json_params: *mut c_char)>>(b"convertXmaPropsToJson")
                {
                    Ok(convert_xma_props_to_json) => {
                        let function = CString::new("ENCODER").unwrap();
                        convert_xma_props_to_json(xma_enc_props, function.as_ptr(), plugin_param.input.as_mut_ptr());
                    }
                    Err(e) => bail!("{}", e),
                }
            }
            Err(e) => bail!("{}", e),
        };
    }

    let plugin_name = CString::new("xrmU30EncPlugin").unwrap().into_raw();

    unsafe {
        let ret = xrmExecPluginFunc(xrm_ctx, plugin_name, func_id, &mut plugin_param);
        if ret != XRM_SUCCESS as i32 {
            bail!("XRM encoder plugin failed to calculate encoder load. error: {}", ret);
        }
    }
    // parse the load from the output buffer of plugin param.
    let output_bytes = &plugin_param.output.map(|i| i as u8);
    let enc_plugin_output = from_utf8(output_bytes)
        .unwrap_or("-1")
        .split(' ')
        .next()
        .expect("split should emit at least one item");
    let load = enc_plugin_output.parse::<i32>().unwrap_or(-1);

    if load == -1 {
        bail!("unable to parse load calculation from XRM encoder plugin");
    }

    Ok(load)
}

fn xlnx_fill_enc_pool_props(cu_pool_prop: &mut xrmCuPoolProperty, enc_count: i32, enc_load: i32) -> Result<(), SimpleError> {
    cu_pool_prop.cuListProp.sameDevice = true;
    cu_pool_prop.cuListNum = 1;
    let mut cu_num = 0;

    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "encoder")?;
    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "ENCODER_MPSOC")?;
    cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
    cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(enc_load);
    cu_num += 1;

    for _ in 0..enc_count {
        strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "kernel_vcu_encoder")?;

        cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
        cu_num += 1
    }
    // we defined 2 cu requests to the properties.
    cu_pool_prop.cuListProp.cuNum = cu_num as i32;

    Ok(())
}

pub fn xlnx_reserve_enc_resource(xlnx_enc_ctx: &mut XlnxEncoderXrmCtx) -> Result<(), SimpleError> {
    // a device has already been chosen, there is no need to assign a reserve id.
    if xlnx_enc_ctx.device_id >= 0 {
        return Ok(());
    }

    let enc_count = 1;
    let mut cu_pool_prop: xrmCuPoolProperty = Default::default();
    xlnx_fill_enc_pool_props(&mut cu_pool_prop, enc_count, xlnx_enc_ctx.enc_load)?;

    unsafe {
        let num_cu_pool = xrmCheckCuPoolAvailableNum(xlnx_enc_ctx.xrm_ctx, &mut cu_pool_prop);
        if num_cu_pool <= 0 {
            bail!("no encoder resources avaliable for allocation")
        }

        xlnx_enc_ctx.xrm_reserve_id = xrmCuPoolReserve(xlnx_enc_ctx.xrm_ctx, &mut cu_pool_prop);
        if xlnx_enc_ctx.xrm_reserve_id == 0 {
            bail!("failed to reserve encode cu pool")
        }
    }

    Ok(())
}

/// Allocates encoder CU based on device_id
fn xlnx_enc_cu_alloc_device_id(xma_enc_props: &mut XmaEncoderProperties, xlnx_enc_ctx: &mut XlnxEncoderXrmCtx) -> Result<(), SimpleError> {
    let mut encode_cu_hw_prop: xrmCuProperty = Default::default();
    let mut encode_cu_sw_prop: xrmCuProperty = Default::default();

    strcpy_to_arr_i8(&mut encode_cu_hw_prop.kernelName, "encoder")?;
    strcpy_to_arr_i8(&mut encode_cu_hw_prop.kernelAlias, "ENCODER_MPSOC")?;
    encode_cu_hw_prop.devExcl = false;
    encode_cu_hw_prop.requestLoad = xrm_precision_1000000_bitmask(xlnx_enc_ctx.enc_load);

    strcpy_to_arr_i8(&mut encode_cu_sw_prop.kernelName, "kernel_vcu_encoder")?;
    encode_cu_sw_prop.devExcl = false;
    encode_cu_sw_prop.requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);

    let ret = unsafe {
        xrmCuAllocFromDev(
            xlnx_enc_ctx.xrm_ctx,
            xlnx_enc_ctx.device_id,
            &mut encode_cu_hw_prop,
            &mut xlnx_enc_ctx.cu_list_res.cuResources[0],
        )
    };
    if ret <= XRM_ERROR {
        bail!("xrm failed to allocate encoder resources on device: {}", xlnx_enc_ctx.device_id);
    }

    let ret = unsafe {
        xrmCuAllocFromDev(
            xlnx_enc_ctx.xrm_ctx,
            xlnx_enc_ctx.device_id,
            &mut encode_cu_sw_prop,
            &mut xlnx_enc_ctx.cu_list_res.cuResources[1],
        )
    };
    if ret <= XRM_ERROR {
        bail!("xrm failed to allocate encoder resources on device: {}", xlnx_enc_ctx.device_id);
    }
    xlnx_enc_ctx.encode_res_in_use = true;

    // Set XMA plugin shared object and device index.
    xma_enc_props.plugin_lib = xlnx_enc_ctx.cu_list_res.cuResources[0].kernelPluginFileName.as_mut_ptr();
    xma_enc_props.dev_index = xlnx_enc_ctx.cu_list_res.cuResources[0].deviceId;

    // Select ddr bank based on xclbin metadata.
    xma_enc_props.ddr_bank_index = -1;
    xma_enc_props.cu_index = xlnx_enc_ctx.cu_list_res.cuResources[1].cuId;
    xma_enc_props.channel_id = xlnx_enc_ctx.cu_list_res.cuResources[1].channelId;

    Ok(())
}

/// Allocated encoder CU based on reserve_id
fn xlnx_enc_cu_alloc_reserve_id(xma_enc_props: &mut XmaEncoderProperties, xlnx_enc_ctx: &mut XlnxEncoderXrmCtx) -> Result<(), SimpleError> {
    // Allocate xrm encoder cu
    let mut encode_cu_list_prop = xrmCuListProperty {
        cuNum: 2,
        ..Default::default()
    };

    strcpy_to_arr_i8(&mut encode_cu_list_prop.cuProps[0].kernelName, "encoder")?;
    strcpy_to_arr_i8(&mut encode_cu_list_prop.cuProps[0].kernelAlias, "ENCODER_MPSOC")?;
    encode_cu_list_prop.cuProps[0].devExcl = false;
    encode_cu_list_prop.cuProps[0].requestLoad = xrm_precision_1000000_bitmask(xlnx_enc_ctx.enc_load);
    encode_cu_list_prop.cuProps[0].poolId = xlnx_enc_ctx.xrm_reserve_id;

    strcpy_to_arr_i8(&mut encode_cu_list_prop.cuProps[1].kernelName, "kernel_vcu_encoder")?;
    encode_cu_list_prop.cuProps[1].devExcl = false;
    encode_cu_list_prop.cuProps[1].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
    encode_cu_list_prop.cuProps[1].poolId = xlnx_enc_ctx.xrm_reserve_id;

    if unsafe { xrmCuListAlloc(xlnx_enc_ctx.xrm_ctx, &mut encode_cu_list_prop, &mut xlnx_enc_ctx.cu_list_res) } != 0 {
        bail!("failed to allocate encode cu list from reserve id {}", xlnx_enc_ctx.xrm_reserve_id)
    }
    xlnx_enc_ctx.encode_res_in_use = true;

    // Set XMA plugin shared object and device index.
    xma_enc_props.plugin_lib = xlnx_enc_ctx.cu_list_res.cuResources[0].kernelPluginFileName.as_mut_ptr();
    xma_enc_props.dev_index = xlnx_enc_ctx.cu_list_res.cuResources[0].deviceId;

    // Select ddr bank based on xclbin metadata.
    xma_enc_props.ddr_bank_index = -1;
    xma_enc_props.cu_index = xlnx_enc_ctx.cu_list_res.cuResources[1].cuId;
    xma_enc_props.channel_id = xlnx_enc_ctx.cu_list_res.cuResources[1].channelId;

    Ok(())
}

fn xlnx_enc_cu_alloc(xma_enc_props: &mut XmaEncoderProperties, xlnx_enc_ctx: &mut XlnxEncoderXrmCtx) -> Result<(), SimpleError> {
    if xlnx_enc_ctx.device_id >= 0 {
        xlnx_enc_cu_alloc_device_id(xma_enc_props, xlnx_enc_ctx)?;
    } else {
        xlnx_enc_cu_alloc_reserve_id(xma_enc_props, xlnx_enc_ctx)?;
    }

    Ok(())
}

/// Attempts to creat encoder session
pub(crate) fn xlnx_create_enc_session(
    xma_enc_props: &mut XmaEncoderProperties,
    xlnx_enc_ctx: &mut XlnxEncoderXrmCtx,
) -> Result<*mut XmaEncoderSession, SimpleError> {
    xlnx_enc_cu_alloc(xma_enc_props, xlnx_enc_ctx)?;

    let enc_session = unsafe { xma_enc_session_create(xma_enc_props) };
    if enc_session.is_null() {
        bail!("failed to create encoder session. Session is null");
    }

    Ok(enc_session)
}

impl Drop for XlnxEncoderXrmCtx {
    fn drop(&mut self) {
        if self.xrm_ctx.is_null() {
            return;
        }
        unsafe {
            if self.encode_res_in_use {
                xrmCuListRelease(self.xrm_ctx, &mut self.cu_list_res);
            }
            if self.xrm_reserve_id != 0 {
                xrmCuPoolRelinquish(self.xrm_ctx, self.xrm_reserve_id);
            }
            xrmDestroyContext(self.xrm_ctx);
        }
    }
}
