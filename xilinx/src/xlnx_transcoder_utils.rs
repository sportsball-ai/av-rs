use crate::{strcpy_to_arr_i8, sys::*, xlnx_dec_utils::*, xlnx_enc_utils::*, xlnx_scal_utils::*, xrm_precision_1000000_bitmask};
use simple_error::{bail, SimpleError};

pub struct XlnxTranscodeLoad {
    pub dec_load: i32,
    pub scal_load: i32,
    pub enc_load: i32,
    pub enc_num: i32,
}

pub struct XlnxTranscodeXrmCtx {
    pub xrm_ctx: xrmContext,
    pub transcode_load: XlnxTranscodeLoad,
    pub reserve_idx: u64,
}

pub fn xlnx_calc_transcode_load(
    xlnx_transcode_xrm_ctx: &mut XlnxTranscodeXrmCtx,
    xma_dec_props: &mut XmaDecoderProperties,
    xma_scal_props: &mut XmaScalerProperties,
    xma_enc_props_list: Vec<&mut XmaEncoderProperties>,
    transcode_cu_pool_prop: &mut xrmCuPoolProperty,
) -> Result<(), SimpleError> {
    xlnx_transcode_xrm_ctx.transcode_load.dec_load = xlnx_calc_dec_load(xlnx_transcode_xrm_ctx.xrm_ctx, xma_dec_props)?;
    xlnx_transcode_xrm_ctx.transcode_load.scal_load = xlnx_calc_scal_load(xlnx_transcode_xrm_ctx.xrm_ctx, xma_scal_props)?;
    for xma_enc_props in xma_enc_props_list {
        xlnx_transcode_xrm_ctx.transcode_load.enc_load += xlnx_calc_enc_load(xlnx_transcode_xrm_ctx.xrm_ctx, xma_enc_props)?;
        xlnx_transcode_xrm_ctx.transcode_load.enc_num += 1;
    }

    xlnx_fill_transcode_pool_props(transcode_cu_pool_prop, &xlnx_transcode_xrm_ctx.transcode_load)?;
    Ok(())
}

fn xlnx_fill_transcode_pool_props(transcode_cu_pool_prop: &mut xrmCuPoolProperty, transcode_load: &XlnxTranscodeLoad) -> Result<(), SimpleError> {
    let mut cu_num = 0;
    transcode_cu_pool_prop.cuListProp.sameDevice = true;
    transcode_cu_pool_prop.cuListNum = 1;

    if transcode_load.dec_load > 0 {
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "decoder")?;
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "DECODER_MPSOC")?;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(transcode_load.dec_load);
        cu_num += 1;

        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "kernel_vcu_decoder")?;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
        cu_num += 1;
    }

    if transcode_load.scal_load > 0 {
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "scaler")?;
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "SCALER_MPSOC")?;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(transcode_load.scal_load);
        cu_num += 1;
    }

    if transcode_load.enc_load > 0 {
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "encoder")?;
        strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "ENCODER_MPSOC")?;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
        transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(transcode_load.enc_load);
        cu_num += 1;

        for _ in 0..transcode_load.enc_num {
            strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelName, "kernel_vcu_encoder")?;
            strcpy_to_arr_i8(&mut transcode_cu_pool_prop.cuListProp.cuProps[cu_num].kernelAlias, "")?;
            transcode_cu_pool_prop.cuListProp.cuProps[cu_num].devExcl = false;
            transcode_cu_pool_prop.cuListProp.cuProps[cu_num].requestLoad = xrm_precision_1000000_bitmask(XRM_MAX_CU_LOAD_GRANULARITY_1000000 as i32);
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
) -> Result<(), SimpleError> {
    let mut transcode_cu_pool_prop: xrmCuPoolProperty = Default::default();
    xlnx_calc_transcode_load(
        xlnx_transcode_xrm_ctx,
        xma_dec_props,
        xma_scal_props,
        xma_enc_props,
        &mut transcode_cu_pool_prop,
    )?;
    unsafe {
        let num_cu_pool = xrmCheckCuPoolAvailableNum(xlnx_transcode_xrm_ctx.xrm_ctx, &mut transcode_cu_pool_prop);
        if num_cu_pool <= 0 {
            bail!("no xilinx hardware resources avaliable for allocation")
        }
        xlnx_transcode_xrm_ctx.reserve_idx = xrmCuPoolReserve(xlnx_transcode_xrm_ctx.xrm_ctx, &mut transcode_cu_pool_prop);
        if xlnx_transcode_xrm_ctx.reserve_idx == 0 {
            bail!("failed to reserve encode cu pool")
        }
    }

    Ok(())
}

impl Drop for XlnxTranscodeXrmCtx {
    fn drop(&mut self) {
        if self.xrm_ctx.is_null() {
            return;
        }
        unsafe {
            if self.reserve_idx != 0 {
                xrmCuPoolRelinquish(self.xrm_ctx, self.reserve_idx);
            }

            xrmDestroyContext(self.xrm_ctx);
        }
    }
}
