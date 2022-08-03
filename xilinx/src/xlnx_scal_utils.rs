use libloading::{Library, Symbol};
use simple_error::{bail, SimpleError};
use std::{ffi::CString, os::raw::c_char, str::from_utf8};

use crate::{strcpy_to_arr_i8, sys::*, xrm_precision_1000000_bitmask};

const SCAL_PLUGIN_NAME: &[u8] = b"xrmU30ScalPlugin\0";

pub struct XlnxScalerXrmCtx {
    pub xrm_reserve_id: u64,
    pub device_id: i32,
    pub scal_load: i32,
    pub scal_res_in_use: bool,
    pub num_outputs: i32,
    pub xrm_ctx: xrmContext,
    pub cu_res: xrmCuResource,
}

#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub fn xlnx_calc_scal_load(xrm_ctx: xrmContext, xma_scal_props: *mut XmaScalerProperties) -> Result<i32, SimpleError> {
    let func_id = 0;
    let mut plugin_param: xrmPluginFuncParam = Default::default();
    unsafe {
        match Library::new("/opt/xilinx/xrm/plugin/libxmaPropsTOjson.so") {
            Ok(lib) => {
                match lib
                    .get::<Symbol<extern "C" fn(props: *mut XmaScalerProperties, function: *const c_char, json_params: *mut c_char)>>(b"convertXmaPropsToJson")
                {
                    Ok(convert_xma_props_to_json) => {
                        let function = CString::new("SCALER").unwrap();
                        convert_xma_props_to_json(xma_scal_props, function.as_ptr(), plugin_param.input.as_mut_ptr());
                    }
                    Err(e) => bail!("{}", e),
                }
            }
            Err(e) => bail!("{}", e),
        };
    }

    unsafe {
        let ret = xrmExecPluginFunc(xrm_ctx, SCAL_PLUGIN_NAME.as_ptr() as *mut i8, func_id, &mut plugin_param);
        if ret != XRM_SUCCESS as i32 {
            bail!("XRM Scaler plugin failed to calculate scaler load. error: {}", ret);
        }
    }
    // parse the load from the output buffer of plugin param.
    let output_bytes = &plugin_param.output.map(|i| i as u8);
    let scal_plugin_output = from_utf8(output_bytes)
        .unwrap_or("-1")
        .split(' ')
        .next()
        .expect("split should emit at least one item");
    let load = scal_plugin_output.parse::<i32>().unwrap_or(-1);

    if load == -1 {
        bail!("unable to parse load calculation from XRM scaler plugin");
    }

    Ok(load)
}

fn xlnx_fill_scal_pool_props(cu_pool_prop: &mut xrmCuPoolProperty, scal_load: i32) -> Result<(), SimpleError> {
    cu_pool_prop.cuListProp.sameDevice = true;
    cu_pool_prop.cuListNum = 1;

    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[0].kernelName, "scaler")?;
    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[0].kernelAlias, "SCALER_MPSOC")?;
    cu_pool_prop.cuListProp.cuProps[0].devExcl = false;
    cu_pool_prop.cuListProp.cuProps[0].requestLoad = xrm_precision_1000000_bitmask(scal_load);
    cu_pool_prop.cuListProp.cuNum = 1;

    Ok(())
}

pub fn xlnx_reserve_scal_resource(xlnx_scal_ctx: &mut XlnxScalerXrmCtx) -> Result<(), SimpleError> {
    // a device has already been chosen, there is no need to assign a reserve id
    if xlnx_scal_ctx.device_id >= 0 {
        return Ok(());
    }

    let mut cu_pool_prop: xrmCuPoolProperty = Default::default();
    xlnx_fill_scal_pool_props(&mut cu_pool_prop, xlnx_scal_ctx.scal_load)?;

    unsafe {
        let num_cu_pool = xrmCheckCuPoolAvailableNum(xlnx_scal_ctx.xrm_ctx, &mut cu_pool_prop);
        if num_cu_pool == 0 {
            bail!("no scaler resources available for allocation")
        }

        xlnx_scal_ctx.xrm_reserve_id = xrmCuPoolReserve(xlnx_scal_ctx.xrm_ctx, &mut cu_pool_prop);
        if xlnx_scal_ctx.xrm_reserve_id == 0 {
            bail!("failed to reserve scaler cu pool")
        }
    }

    Ok(())
}

pub(crate) fn xlnx_create_scal_session(
    xma_scal_props: &mut XmaScalerProperties,
    xlnx_scal_ctx: &mut XlnxScalerXrmCtx,
) -> Result<*mut XmaScalerSession, SimpleError> {
    xma_scal_props.num_outputs = xlnx_scal_ctx.num_outputs;
    xlnx_scal_cu_alloc(xma_scal_props, xlnx_scal_ctx)?;

    let scal_session = unsafe { xma_scaler_session_create(xma_scal_props) };
    if scal_session.is_null() {
        bail!("failed to create scaler session. Session is null")
    }

    Ok(scal_session)
}

fn xlnx_scal_cu_alloc(xma_scal_props: &mut XmaScalerProperties, xlnx_scal_ctx: &mut XlnxScalerXrmCtx) -> Result<(), SimpleError> {
    let mut scaler_cu_prop: xrmCuProperty = Default::default();

    strcpy_to_arr_i8(&mut scaler_cu_prop.kernelName, "scaler")?;
    strcpy_to_arr_i8(&mut scaler_cu_prop.kernelAlias, "SCALER_MPSOC")?;
    scaler_cu_prop.devExcl = false;
    scaler_cu_prop.requestLoad = xrm_precision_1000000_bitmask(xlnx_scal_ctx.scal_load);

    let ret: i32;
    if xlnx_scal_ctx.device_id < 0 {
        scaler_cu_prop.poolId = xlnx_scal_ctx.xrm_reserve_id;
        ret = unsafe { xrmCuAlloc(xlnx_scal_ctx.xrm_ctx, &mut scaler_cu_prop, &mut xlnx_scal_ctx.cu_res) };
    } else {
        ret = unsafe { xrmCuAllocFromDev(xlnx_scal_ctx.xrm_ctx, xlnx_scal_ctx.device_id, &mut scaler_cu_prop, &mut xlnx_scal_ctx.cu_res) };
    }
    if ret != XRM_SUCCESS as i32 {
        bail!(
            "xrm alloc failed {} to asllocate scaler cu from reserve id {}",
            ret,
            xlnx_scal_ctx.xrm_reserve_id
        );
    }
    xlnx_scal_ctx.scal_res_in_use = true;

    // Set xma plugin shared object and device index
    xma_scal_props.plugin_lib = xlnx_scal_ctx.cu_res.kernelPluginFileName.as_mut_ptr();
    xma_scal_props.dev_index = xlnx_scal_ctx.cu_res.deviceId;
    xma_scal_props.cu_index = xlnx_scal_ctx.cu_res.cuId;
    xma_scal_props.channel_id = xlnx_scal_ctx.cu_res.channelId;
    // XMA to select the ddr bank based on xclbin meta data
    xma_scal_props.ddr_bank_index = -1;

    Ok(())
}

impl Drop for XlnxScalerXrmCtx {
    fn drop(&mut self) {
        if self.xrm_ctx.is_null() {
            return;
        }
        unsafe {
            if self.scal_res_in_use {
                xrmCuRelease(self.xrm_ctx, &mut self.cu_res);
            }
        }
    }
}
