use crate::{sys::*, xlnx_error::*, xlnx_scal_utils::*};
use simple_error::SimpleError;

pub const SCAL_MAX_ABR_CHANNELS: usize = 8;
pub const SCAL_MAX_SESSIONS: usize = 2;
pub const SCAL_RATE_STRING_LEN: usize = 8;

pub struct XlnxScaler {
    scal_session: *mut XmaScalerSession,
    pub out_frame_list: Vec<*mut XmaFrame>,
    pub xrm_scalres_count: i32,
    pub flush_sent: bool,
}

impl XlnxScaler {
    pub fn new(xma_scal_props: &mut XmaScalerProperties, xlnx_scal_ctx: &mut XlnxScalerXrmCtx) -> Result<Self, SimpleError> {
        let scal_session = xlnx_create_scal_session(xma_scal_props, xlnx_scal_ctx)?;

        let mut out_frame_list = Vec::new();

        for i in 0..SCAL_MAX_ABR_CHANNELS {
            unsafe {
                let mut frame_props = XmaFrameProperties {
                    format: (*xma_scal_props).output[i].format,
                    width: (*xma_scal_props).output[i].width,
                    height: (*xma_scal_props).output[i].height,
                    bits_per_pixel: 8,
                    ..Default::default()
                };

                let xma_frame = xma_frame_alloc(&mut frame_props, true);

                // Loop through the planes. no buffer shouold be allocated yet.
                // Since this will be used in a pipeline, xvbm will allocate the buffers.
                // So we need to specify to use device buffers.
                for j in 0..2 {
                    (*xma_frame).data[j].buffer_type = XmaBufferType_XMA_DEVICE_BUFFER_TYPE;
                    (*xma_frame).data[j].is_clone = true;
                }
                out_frame_list.push(xma_frame)
            }
        }

        Ok(Self {
            scal_session,
            out_frame_list,
            xrm_scalres_count: xma_scal_props.num_outputs,
            flush_sent: false,
        })
    }

    /// Sends frame to xilinx scaler using xma plugin
    ///
    /// @in_frame: decoded frame input to be scaled.
    fn send_frame(&mut self, in_frame: *mut XmaFrame) -> Result<(), XlnxError> {
        let ret = unsafe { xma_scaler_session_send_frame(self.scal_session, in_frame) };
        if ret != XMA_SUCCESS as i32 {
            return Err(XlnxError::new(ret, Some("error sending frame to scaler".to_string())));
        }

        Ok(())
    }

    /// Receive list of scaled frames from the scalar using xma plugin
    ///
    /// Resulting list is stored in this object's out_frame_list parameter.
    fn recv_frame(&mut self) -> Result<(), XlnxError> {
        let ret = unsafe { xma_scaler_session_recv_frame_list(self.scal_session, self.out_frame_list.as_mut_ptr()) };
        if ret != XMA_SUCCESS as i32 {
            return Err(XlnxError::new(ret, Some("error receiving frame list from scaler".to_string())));
        }

        Ok(())
    }

    /// Processes a frame with the xilinx scaler xma plugin.
    ///
    /// @in_frame: decoded frame input to be scaled.
    /// The resulting output list is populated in this object's out_frame_list
    pub fn process_frame(&mut self, in_frame: *mut XmaFrame) -> Result<(), XlnxError> {
        match self.send_frame(in_frame) {
            Ok(_) => {
                self.recv_frame()?;
            }
            Err(e) => match e.err {
                XlnxErrorType::FlushAgain => {
                    self.recv_frame()?;
                }
                _ => return Err(e),
            },
        }

        Ok(())
    }
}

impl Drop for XlnxScaler {
    fn drop(&mut self) {
        unsafe {
            if !self.scal_session.is_null() {
                xma_scaler_session_destroy(self.scal_session);
            }
            for &f in &self.out_frame_list {
                if !f.is_null() {
                    xma_frame_free(f);
                }
            }
        }
    }
}

#[cfg(test)]
mod scaler_tests {
    use crate::{tests::*, xlnx_scal_props::*, xlnx_scal_utils::*, xlnx_scaler::*};

    #[test]
    fn test_abr_scale() {
        let mut frame_props = XmaFrameProperties {
            format: XmaFormatType_XMA_YUV420_FMT_TYPE,
            width: 1280,
            height: 720,
            bits_per_pixel: 8,
            ..Default::default()
        };
        // read the raw yuv into xma_frames, This file has 300 frames.
        let (xma_frames, _y_component, _x_component) = read_raw_yuv_420_p(RAW_YUV_P_FILE_PATH, &mut frame_props, 300);

        initialize();

        // create a Xlnx scalar's context
        let scal_props = XlnxScalerProperties {
            in_width: 1280,
            in_height: 720,
            framerate: XmaFraction { numerator: 1, denominator: 25 },
            nb_outputs: 3,
            out_width: [1280, 852, 640, 0, 0, 0, 0, 0],
            out_height: [720, 480, 360, 0, 0, 0, 0, 0],
            enable_pipeline: 0,
            log_level: 3,
            latency_logging: 1,
        };

        let mut xma_scal_props = XlnxXmaScalerProperties::from(scal_props);

        let xrm_ctx = unsafe { xrmCreateContext(XRM_API_VERSION_1) };
        let scal_load = xlnx_calc_scal_load(xrm_ctx, xma_scal_props.as_mut()).unwrap();
        let mut xlnx_scal_ctx = XlnxScalerXrmCtx::new(xrm_ctx, None, None, scal_load, 3);

        xlnx_reserve_scal_resource(&mut xlnx_scal_ctx).unwrap();

        // create xlnx scaler
        let mut scaler = XlnxScaler::new(xma_scal_props.as_mut(), &mut xlnx_scal_ctx).unwrap();

        let mut processed_frame_count = 0;

        for xma_frame in xma_frames {
            match scaler.process_frame(xma_frame) {
                Ok(_) => {
                    // successfully scaled frame.
                    processed_frame_count += 1;
                    // clear xvbm buffers for each return frame
                    for i in 0..xlnx_scal_ctx.num_outputs as usize {
                        unsafe {
                            let handle: XvbmBufferHandle = (*scaler.out_frame_list[i]).data[0].buffer;
                            xvbm_buffer_pool_entry_free(handle);
                        }
                    }
                }
                Err(e) => match e.err {
                    XlnxErrorType::SendMoreData => {}
                    _ => panic!("scalar processing has failed with error {:?}", e),
                },
            };
            if !xma_frame.is_null() {
                unsafe { xma_frame_free(xma_frame) };
            }
        }

        //flush the buffer
        loop {
            unsafe {
                // prepare flush frame.
                let flush_frame = xma_frame_alloc(&mut frame_props, true);
                (*flush_frame).is_last_frame = 1;
                (*flush_frame).pts = u64::MAX;

                let flush_frame = scopeguard::guard(flush_frame, |frame| {
                    xma_frame_free(frame); // if we allocated a flush frame it needs to be dealocated.
                });

                match scaler.process_frame(*flush_frame) {
                    Ok(_) => {
                        // successfully scaled frame.
                        processed_frame_count += 1;
                        // clear xvbm buffers for each return frame
                        for i in 0..xlnx_scal_ctx.num_outputs as usize {
                            let handle: XvbmBufferHandle = (*scaler.out_frame_list[i]).data[0].buffer;
                            xvbm_buffer_pool_entry_free(handle);
                        }
                    }
                    Err(e) => match e.err {
                        XlnxErrorType::EOS => break,
                        XlnxErrorType::FlushAgain => {}
                        _ => panic!("error receiving flushing xilinx scaler: {:?}", e),
                    },
                }
            }
        }

        assert_eq!(processed_frame_count, 300);
    }
}
