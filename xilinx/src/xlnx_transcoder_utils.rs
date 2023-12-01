use crate::{strcpy_to_arr_i8, sys::*, xlnx_dec_utils::*, xlnx_enc_utils::*, xlnx_scal_utils::*, xrm_precision_1000000_bitmask};
use simple_error::{bail, SimpleError};

/// This struct is used to hold the load information for the transcode
/// cu pool.  The load information is used for reservation and allocation
/// of the transcode cu pool.
pub struct XlnxTranscodeLoad {
    pub dec_load: i32,
    pub scal_load: i32,

    /// The sum of these loads is used for resource reservation, however the
    /// individual loads must be used to allocate the encoder for each varient.
    pub enc_loads: Vec<i32>,
    pub enc_num: i32,
}

pub struct XlnxTranscodeXrmCtx {
    pub xrm_ctx: xrmContext,
    pub transcode_load: XlnxTranscodeLoad,
    pub xrm_reserve_id: Option<u64>,
    pub device_id: Option<u32>,
}

impl XlnxTranscodeXrmCtx {
    pub fn new() -> Self {
        let xrm_ctx = unsafe { xrmCreateContext(XRM_API_VERSION_1) };

        Self {
            xrm_ctx,
            transcode_load: XlnxTranscodeLoad {
                dec_load: 0,
                scal_load: 0,
                enc_loads: vec![],
                enc_num: 0,
            },
            xrm_reserve_id: None,
            device_id: None,
        }
    }
}

pub fn xlnx_calc_transcode_load(
    xlnx_transcode_xrm_ctx: &mut XlnxTranscodeXrmCtx,
    xma_dec_props: &mut XmaDecoderProperties,
    xma_scal_props: &mut XmaScalerProperties,
    xma_enc_props_list: Vec<&mut XmaEncoderProperties>,
    transcode_cu_pool_prop: &mut xrmCuPoolPropertyV2,
) -> Result<(), SimpleError> {
    xlnx_transcode_xrm_ctx.transcode_load.dec_load = xlnx_calc_dec_load(xlnx_transcode_xrm_ctx.xrm_ctx, xma_dec_props)?;
    xlnx_transcode_xrm_ctx.transcode_load.scal_load = xlnx_calc_scal_load(xlnx_transcode_xrm_ctx.xrm_ctx, xma_scal_props)?;
    for xma_enc_props in xma_enc_props_list {
        let enc_load = xlnx_calc_enc_load(xlnx_transcode_xrm_ctx.xrm_ctx, xma_enc_props)?;
        xlnx_transcode_xrm_ctx.transcode_load.enc_loads.push(enc_load);
        xlnx_transcode_xrm_ctx.transcode_load.enc_num += 1;
    }

    xlnx_fill_transcode_pool_props(transcode_cu_pool_prop, &xlnx_transcode_xrm_ctx.transcode_load, xlnx_transcode_xrm_ctx.device_id)?;
    Ok(())
}

fn xlnx_fill_transcode_pool_props(
    transcode_cu_pool_prop: &mut xrmCuPoolPropertyV2,
    transcode_load: &XlnxTranscodeLoad,
    device_id: Option<u32>,
) -> Result<(), SimpleError> {
    let mut cu_num = 0;
    transcode_cu_pool_prop.cuListNum = 1;
    let mut device_info = 0;
    if let Some(device_id) = device_id {
        device_info = (device_id << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) as u64
            | ((XRM_DEVICE_INFO_CONSTRAINT_TYPE_HARDWARE_DEVICE_INDEX as u64) << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
    }

    if transcode_load.dec_load > 0 {
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "decoder")?;
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "DECODER_MPSOC")?;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(transcode_load.dec_load);
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].deviceInfo = device_info;
        cu_num += 1;

        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "kernel_vcu_decoder")?;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].deviceInfo = device_info;
        cu_num += 1;
    }

    if transcode_load.scal_load > 0 {
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "scaler")?;
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "SCALER_MPSOC")?;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(transcode_load.scal_load);
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].deviceInfo = device_info;
        cu_num += 1;
    }

    // total encoder load is the sum of all of the encoder loads
    let total_enc_load = transcode_load.enc_loads.iter().sum::<i32>();
    if total_enc_load > 0 {
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "encoder")?;
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "ENCODER_MPSOC")?;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(total_enc_load);
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].deviceInfo = device_info;
        cu_num += 1;

        for _ in 0..transcode_load.enc_num {
            strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "kernel_vcu_encoder")?;
            strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "")?;
            transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
            transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
            transcode_cu_pool_prop.cuListProp.cuProps[cu_num].deviceInfo = device_info;
            cu_num += 1;
        }
    }

    transcode_cu_pool_prop.cuListProp.cuNum = cu_num as i32;
    Ok(())
}

pub fn xlnx_reserve_transcode_resource(
    xlnx_transcode_xrm_ctx: &mut XlnxTranscodeXrmCtx,
    xma_dec_props: &mut XmaDecoderProperties,
    xma_scal_props: &mut XmaScalerProperties,
    xma_enc_props: Vec<&mut XmaEncoderProperties>,
) -> Result<Box<xrmCuPoolResInforV2>, SimpleError> {
    let mut transcode_cu_pool_prop: Box<xrmCuPoolPropertyV2> = Box::new(Default::default());
    let mut cu_pool_res_infor: Box<xrmCuPoolResInforV2> = Box::new(Default::default());
    xlnx_calc_transcode_load(
        xlnx_transcode_xrm_ctx,
        xma_dec_props,
        xma_scal_props,
        xma_enc_props,
        &mut transcode_cu_pool_prop,
    )?;
    unsafe {
        let num_cu_pool = xrmCheckCuPoolAvailableNumV2(xlnx_transcode_xrm_ctx.xrm_ctx, transcode_cu_pool_prop.as_mut());
        if num_cu_pool <= 0 {
            bail!("no xilinx hardware resources avaliable for allocation")
        }
        let xrm_reserve_id = xrmCuPoolReserveV2(xlnx_transcode_xrm_ctx.xrm_ctx, transcode_cu_pool_prop.as_mut(), cu_pool_res_infor.as_mut());
        if xrm_reserve_id == 0 {
            bail!("failed to reserve transcode cu pool")
        }
        xlnx_transcode_xrm_ctx.xrm_reserve_id = Some(xrm_reserve_id);
    }

    Ok(cu_pool_res_infor)
}

impl Default for XlnxTranscodeXrmCtx {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for XlnxTranscodeXrmCtx {
    fn drop(&mut self) {
        if self.xrm_ctx.is_null() {
            return;
        }
        unsafe {
            if let Some(xrm_reserve_id) = self.xrm_reserve_id {
                let _ = xrmCuPoolRelinquishV2(self.xrm_ctx, xrm_reserve_id);
            }

            xrmDestroyContext(self.xrm_ctx);
        }
    }
}
