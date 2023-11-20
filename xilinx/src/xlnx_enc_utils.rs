use crate::{strcpy_to_arr_i8, sys::*, xrm_precision_1000000_bitmask};
use libloading::{Library, Symbol};
use simple_error::{bail, SimpleError};
use std::{ffi::CString, os::raw::c_char, str::from_utf8};

const ENC_PLUGIN_NAME: &[u8] = b"xrmU30EncPlugin\0";

pub struct XlnxEncoderXrmCtx {
    pub xrm_reserve_id: Option<u64>,
    pub device_id: Option<u64>,
    pub enc_load: i32,
    pub encode_res_in_use: bool,
    pub xrm_ctx: xrmContext,
    pub cu_list_res: xrmCuListResourceV2,
}

/// Calculates the encoder load uing the xrmU30Enc plugin.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
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

    unsafe {
        let ret = xrmExecPluginFunc(xrm_ctx, ENC_PLUGIN_NAME.as_ptr() as *mut i8, func_id, &mut plugin_param);
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

fn xlnx_fill_enc_pool_props(cu_pool_prop: &mut xrmCuPoolPropertyV2, enc_count: i32, enc_load: i32, device_id: Option<u64>) -> Result<(), SimpleError> {
    cu_pool_prop.cuListNum = 1;
    let mut cu_num = 0;
    let mut device_info = 0;

    if let Some(device_id) = device_id {
        device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT)
            | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
    }

    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "encoder")?;
    strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "ENCODER_MPSOC")?;
    cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
    cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(enc_load);
    cu_pool_prop.cuListProp.cuProps[cu_num].deviceInfo = device_info as _;

    cu_num += 1;

    for _ in 0..enc_count {
        strcpy_to_arr_i8(&mut cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "kernel_vcu_encoder")?;

        cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
        cu_pool_prop.cuListProp.cuProps[cu_num].deviceInfo = device_info as _;
        cu_num += 1
    }
    // we defined multiple cu requests to the properties.
    cu_pool_prop.cuListProp.cuNum = cu_num as i32;

    Ok(())
}

pub fn xlnx_reserve_enc_resource(xlnx_enc_ctx: &mut XlnxEncoderXrmCtx) -> Result<xrmCuPoolResInforV2, SimpleError> {
    let enc_count = 1;
    let mut cu_pool_prop: xrmCuPoolPropertyV2 = Default::default();
    let mut cu_pool_res_infor: xrmCuPoolResInforV2 = Default::default();
    xlnx_fill_enc_pool_props(&mut cu_pool_prop, enc_count, xlnx_enc_ctx.enc_load, xlnx_enc_ctx.device_id)?;

    unsafe {
        let num_cu_pool = xrmCheckCuPoolAvailableNumV2(xlnx_enc_ctx.xrm_ctx, &mut cu_pool_prop);
        if num_cu_pool <= 0 {
            bail!("no encoder resources avaliable for allocation")
        }

        let xrm_reserve_id: u64 = xrmCuPoolReserveV2(xlnx_enc_ctx.xrm_ctx, &mut cu_pool_prop, &mut cu_pool_res_infor);
        if xrm_reserve_id == 0 {
            bail!("failed to reserve encode cu pool")
        }
        xlnx_enc_ctx.xrm_reserve_id = Some(xrm_reserve_id);
    }

    Ok(cu_pool_res_infor)
}

/// Allocated encoder CU
fn xlnx_enc_cu_alloc(xma_enc_props: &mut XmaEncoderProperties, xlnx_enc_ctx: &mut XlnxEncoderXrmCtx) -> Result<(), SimpleError> {
    // Allocate xrm encoder cu
    let mut encode_cu_list_prop = xrmCuListPropertyV2 {
        cuNum: 2,
        ..Default::default()
    };

    strcpy_to_arr_i8(&mut encode_cu_list_prop.cuProps[0].kernelName, "encoder")?;
    strcpy_to_arr_i8(&mut encode_cu_list_prop.cuProps[0].kernelAlias, "ENCODER_MPSOC")?;
    encode_cu_list_prop.cuProps[0].devExcl = false;
    encode_cu_list_prop.cuProps[0].requestLoad = xrm_precision_1000000_bitmask(xlnx_enc_ctx.enc_load);

    strcpy_to_arr_i8(&mut encode_cu_list_prop.cuProps[1].kernelName, "kernel_vcu_encoder")?;
    encode_cu_list_prop.cuProps[1].devExcl = false;
    encode_cu_list_prop.cuProps[1].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);

    match (xlnx_enc_ctx.device_id, xlnx_enc_ctx.xrm_reserve_id) {
        (Some(device_id), Some(xrm_reserve_id)) => {
            let device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT)
                | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
            encode_cu_list_prop.cuProps[0].deviceInfo = device_info as _;
            encode_cu_list_prop.cuProps[0].poolId = xrm_reserve_id;
            encode_cu_list_prop.cuProps[1].deviceInfo = device_info as _;
            encode_cu_list_prop.cuProps[1].poolId = xrm_reserve_id;
        }
        (Some(device_id), None) => {
            let device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT)
                | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
            encode_cu_list_prop.cuProps[0].deviceInfo = device_info as _;
            encode_cu_list_prop.cuProps[1].deviceInfo = device_info as _;
        }
        (None, Some(reserve_id)) => {
            encode_cu_list_prop.cuProps[0].poolId = reserve_id;
            encode_cu_list_prop.cuProps[1].poolId = reserve_id;
        }
        (None, None) => {
            // use the zero indexed device as the default if no device or reserve_id have been provided
            let device_info = (XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT;
            encode_cu_list_prop.cuProps[0].deviceInfo = device_info as _;
            encode_cu_list_prop.cuProps[1].deviceInfo = device_info as _;
        }
    }

    if unsafe { xrmCuListAllocV2(xlnx_enc_ctx.xrm_ctx, &mut encode_cu_list_prop, &mut xlnx_enc_ctx.cu_list_res) } != XRM_SUCCESS as _ {
        bail!(
            "failed to allocate encode cu list from reserve id {:?} and device id {:?}",
            xlnx_enc_ctx.xrm_reserve_id,
            xlnx_enc_ctx.device_id
        );
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
            if let Some(xrm_reserve_id) = self.xrm_reserve_id {
                let _ = xrmCuPoolRelinquishV2(self.xrm_ctx, xrm_reserve_id);
            }
            if self.encode_res_in_use {
                let _ = xrmCuListReleaseV2(self.xrm_ctx, &mut self.cu_list_res);
            }
        }
    }
}
