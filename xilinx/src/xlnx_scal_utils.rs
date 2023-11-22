use libloading::{Library, Symbol};
use simple_error::{bail, SimpleError};
use std::{ffi::CString, os::raw::c_char, str::from_utf8};

use crate::{strcpy_to_arr_i8, sys::*, xrm_precision_1000000_bitmask};

const SCAL_PLUGIN_NAME: &[u8] = b"xrmU30ScalPlugin\0";

pub struct XlnxScalerXrmCtx {
    pub xrm_reserve_id: Option<u64>,
    pub device_id: Option<u32>,
    pub scal_load: i32,
    pub(crate) scal_res_in_use: bool,
    pub num_outputs: i32,
    pub(crate) xrm_ctx: xrmContext,
    pub(crate) cu_res: Box<xrmCuResourceV2>,
}

impl XlnxScalerXrmCtx {
    pub fn new(xrm_ctx: xrmContext, device_id: Option<u32>, reserve_id: Option<u64>, scal_load: i32, num_outputs: i32) -> Self {
        Self {
            xrm_reserve_id: reserve_id,
            device_id,
            scal_load,
            scal_res_in_use: false,
            num_outputs,
            xrm_ctx,
            cu_res: Box::new(Default::default()),
        }
    }
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

fn xlnx_fill_scal_pool_props(cu_pool_prop: &mut xrmCuPoolPropertyV2, scal_load: i32, device_id: Option<u32>) -> Result<(), SimpleError> {
    cu_pool_prop.cuListNum = 1;

    let mut device_info = 0;

    if let Some(device_id) = device_id {
        device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) as u64
            | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
    }

    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[0].kernelName, "scaler")?;
    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[0].kernelAlias, "SCALER_MPSOC")?;
    cu_pool_prop.cuListProp.cuProps[0].devExcl = false;
    cu_pool_prop.cuListProp.cuProps[0].requestLoad = xrm_precision_1000000_bitmask(scal_load);
    cu_pool_prop.cuListProp.cuProps[0].deviceInfo = device_info as _;
    cu_pool_prop.cuListProp.cuNum = 1;

    Ok(())
}

pub fn xlnx_reserve_scal_resource(xlnx_scal_ctx: &mut XlnxScalerXrmCtx) -> Result<Box<xrmCuPoolResInforV2>, SimpleError> {
    let mut cu_pool_prop: Box<xrmCuPoolPropertyV2> = Box::new(Default::default());
    let mut cu_pool_res_infor: Box<xrmCuPoolResInforV2> = Box::new(Default::default());
    xlnx_fill_scal_pool_props(&mut cu_pool_prop, xlnx_scal_ctx.scal_load, xlnx_scal_ctx.device_id)?;

    unsafe {
        let num_cu_pool = xrmCheckCuPoolAvailableNumV2(xlnx_scal_ctx.xrm_ctx, cu_pool_prop.as_mut());
        if num_cu_pool == 0 {
            bail!("no scaler resources available for allocation")
        }

        let xrm_reserve_id = xrmCuPoolReserveV2(xlnx_scal_ctx.xrm_ctx, cu_pool_prop.as_mut(), cu_pool_res_infor.as_mut());
        if xrm_reserve_id == 0 {
            bail!("failed to reserve scaler cu pool")
        }
        xlnx_scal_ctx.xrm_reserve_id = Some(xrm_reserve_id);
    }

    Ok(cu_pool_res_infor)
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
    let mut scaler_cu_prop: Box<xrmCuPropertyV2> = Box::new(Default::default());

    strcpy_to_arr_i8(&mut scaler_cu_prop.kernelName, "scaler")?;
    strcpy_to_arr_i8(&mut scaler_cu_prop.kernelAlias, "SCALER_MPSOC")?;
    scaler_cu_prop.devExcl = false;
    scaler_cu_prop.requestLoad = xrm_precision_1000000_bitmask(xlnx_scal_ctx.scal_load);

    match (xlnx_scal_ctx.device_id, xlnx_scal_ctx.xrm_reserve_id) {
        (Some(device_id), Some(xrm_reserve_id)) => {
            let device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) as u64
                | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
            scaler_cu_prop.deviceInfo = device_info as _;
            scaler_cu_prop.poolId = xrm_reserve_id;
        }
        (Some(device_id), None) => {
            let device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) as u64
                | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
            scaler_cu_prop.deviceInfo = device_info as _;
        }
        (None, Some(reserve_id)) => {
            scaler_cu_prop.poolId = reserve_id;
        }
        (None, None) => {
            bail!("failed to allocate scaler cu: no device id or reserve id provided");
        }
    }

    if unsafe { xrmCuAllocV2(xlnx_scal_ctx.xrm_ctx, scaler_cu_prop.as_mut(), xlnx_scal_ctx.cu_res.as_mut()) } != XRM_SUCCESS as _ {
        bail!(
            "failed to allocate scaler cu from reserve id {:?} and device id {:?}",
            xlnx_scal_ctx.xrm_reserve_id,
            xlnx_scal_ctx.device_id
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
            if let Some(xrm_reserve_id) = self.xrm_reserve_id {
                let _ = xrmCuPoolRelinquishV2(self.xrm_ctx, xrm_reserve_id);
            }
            if self.scal_res_in_use {
                xrmCuReleaseV2(self.xrm_ctx, self.cu_res.as_mut());
            }
        }
    }
}
