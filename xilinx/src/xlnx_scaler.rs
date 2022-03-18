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
        xlnx_reserve_scal_resource(xlnx_scal_ctx)?;
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

                let mut xma_frame = xma_frame_alloc(&mut frame_props, true);

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
                XlnxErrorType::XlnxFlushAgain => {
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
            for i in 0..SCAL_MAX_ABR_CHANNELS {
                if !self.out_frame_list[i].is_null() {
                    xma_frame_free(self.out_frame_list[i]);
                }
            }
        }
    }
}
