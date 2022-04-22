use crate::{sys::*, xlnx_enc_utils::*, xlnx_error::*};
use simple_error::SimpleError;

pub struct XlnxEncoder {
    enc_session: *mut XmaEncoderSession,
    pub out_buffer: *mut XmaDataBuffer,
    pub flush_frame_sent: bool,
}

impl XlnxEncoder {
    pub fn new(xma_enc_props: &mut XmaEncoderProperties, xlnx_enc_ctx: &mut XlnxEncoderXrmCtx) -> Result<Self, SimpleError> {
        xlnx_reserve_enc_resource(xlnx_enc_ctx)?;

        let enc_session = xlnx_create_enc_session(xma_enc_props, xlnx_enc_ctx)?;

        let out_buffer = unsafe { xma_data_buffer_alloc(0, true) };

        Ok(Self {
            enc_session,
            out_buffer,
            flush_frame_sent: false,
        })
    }

    /// Sends raw frames to Xilinx Encoder plugin and receives back decoded frames.  
    pub fn process_frame(&mut self, enc_in_frame: *mut XmaFrame, enc_null_frame: bool, enc_out_size: &mut i32) -> Result<(), XlnxError> {
        if !self.flush_frame_sent {
            match self.send_frame(enc_in_frame) {
                Ok(_) => {}
                Err(e) => {
                    if enc_null_frame {
                        self.flush_frame_sent = true;
                    }
                    return Err(e);
                }
            }
        }

        match self.recv_frame(enc_out_size) {
            Ok(_) => {
                if enc_null_frame {
                    self.flush_frame_sent = true;
                }
                Ok(())
            }
            Err(e) => Err(e),
        }
    }

    /// Sends raw frames to the encoder to be processed
    fn send_frame(&mut self, frame: *mut XmaFrame) -> Result<(), XlnxError> {
        unsafe {
            let ret = xma_enc_session_send_frame(self.enc_session, frame);
            if ret == XMA_ERROR {
                let handle: XvbmBufferHandle = (*frame).data[0].buffer;
                if !handle.is_null() {
                    xvbm_buffer_pool_entry_free(handle);
                }
            }

            if ret != XMA_SUCCESS as i32 {
                return Err(XlnxError::new(ret, Some("error sending frame to encoder".to_string())));
            }
        }

        Ok(())
    }

    /// Receives encoded frames from encoder.
    fn recv_frame(&mut self, enc_out_size: &mut i32) -> Result<(), XlnxError> {
        let ret = unsafe { xma_enc_session_recv_data(self.enc_session, self.out_buffer, enc_out_size) };
        if ret != XMA_SUCCESS as i32 {
            return Err(XlnxError::new(ret, Some("error receiving frame from encoder".to_string())));
        }

        Ok(())
    }
}

impl Drop for XlnxEncoder {
    fn drop(&mut self) {
        unsafe {
            if !self.enc_session.is_null() {
                xma_enc_session_destroy(self.enc_session);
            }
            if !self.out_buffer.is_null() {
                xma_data_buffer_free(self.out_buffer);
            }
        }
    }
}

#[cfg(test)]
mod encoder_tests {
    use crate::{tests::*, xlnx_enc_props::*, xlnx_enc_utils::*, xlnx_encoder::*};

    fn encode_raw(codec_id: i32, profile: i32, level: i32) -> i32 {
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

        // create a Xlnx encoder's context
        let mut enc_props = Box::new(XlnxEncoderProperties {
            cpb_size: 2.0,
            initial_delay: 1.0,
            max_bitrate: 4000,
            bit_rate: 4000,
            width: 1280,
            height: 720,
            framerate: XmaFraction { numerator: 1, denominator: 25 },
            gop_size: 120,
            slice_qp: -1,
            min_qp: 0,
            max_qp: 51,
            codec_id,
            control_rate: 1,
            custom_rc: 0,
            gop_mode: 0,
            gdr_mode: 0,
            num_bframes: 2,
            idr_period: 120,
            profile,
            level,
            tier: 0,
            num_slices: 1,
            dependent_slice: false,
            slice_size: 0,
            temporal_aq: 1,
            spatial_aq: 1,
            spatial_aq_gain: 50,
            qp_mode: 1,
            filler_data: false,
            aspect_ratio: XlnxAspectRatio::AspectRatio16x9,
            scaling_list: 1,
            entropy_mode: 1,
            loop_filter: true,
            constrained_intra_pred: false,
            prefetch_buffer: true,
            tune_metrics: false,
            num_cores: 0,
            latency_logging: 1,
            enable_hw_buf: 0,
            enc_options: String::new(),
            enc_options_ptr: std::ptr::null(),
        });

        let enc_params: [XmaParameter; MAX_ENC_PARAMS] = Default::default();
        let mut enc_params = Box::new(enc_params);

        let mut xma_enc_props = xlnx_create_xma_enc_props(&mut enc_props, &mut enc_params).unwrap();

        let xrm_ctx = unsafe { xrmCreateContext(XRM_API_VERSION_1) };

        let cu_list_res: xrmCuListResource = Default::default();
        let enc_load = xlnx_calc_enc_load(xrm_ctx, &mut *xma_enc_props).unwrap();

        let mut xlnx_enc_ctx = XlnxEncoderXrmCtx {
            xrm_reserve_id: 0,
            device_id: -1,
            enc_load,
            encode_res_in_use: false,
            xrm_ctx,
            cu_list_res,
        };

        // create xlnx encoder
        let mut encoder = XlnxEncoder::new(&mut *xma_enc_props, &mut xlnx_enc_ctx).unwrap();

        let mut processed_frame_count = 0;

        for xma_frame in xma_frames {
            let mut enc_out_size = 0;
            match encoder.process_frame(xma_frame, false, &mut enc_out_size) {
                Ok(_) => {
                    if enc_out_size > 0 {
                        //successfully encoded frame
                        processed_frame_count += 1;
                    } else {
                        panic!("no data received from encoder")
                    }
                }
                Err(e) => match e.err {
                    XlnxErrorType::XlnxSendMoreData => {}
                    XlnxErrorType::XlnxTryAgain => {}
                    _ => panic!("encoder processing has failed with error {:?}", e),
                },
            }
        }

        //allocate a null frame without a buffer to start flushing
        let null_frame = unsafe { xma_frame_alloc(&mut frame_props, true) };
        loop {
            let mut enc_out_size = 0;
            unsafe {
                (*null_frame).is_last_frame = 1;
                (*null_frame).pts = u64::MAX;
            }

            match encoder.process_frame(null_frame, true, &mut enc_out_size) {
                Ok(_) => {
                    processed_frame_count += 1;
                    break;
                } // if flush was successful the first time, break
                Err(e) => match e.err {
                    XlnxErrorType::XlnxFlushAgain => {}
                    XlnxErrorType::XlnxEOS => break,
                    _ => panic!("error sending flush frame to encoder {:?}", e),
                },
            }
        }
        //free the null frame
        unsafe { xma_frame_free(null_frame) };

        loop {
            let mut enc_out_size = 0;
            match encoder.recv_frame(&mut enc_out_size) {
                Ok(_) => {
                    processed_frame_count += 1;
                }
                Err(e) => match e.err {
                    XlnxErrorType::XlnxEOS => break, //we have hit the end of the stream,
                    XlnxErrorType::XlnxTryAgain => {}
                    _ => panic!("error receiving frame while flushing {:?}", e),
                },
            }
        }

        processed_frame_count
    }
    #[test]
    fn test_hevc_encode() {
        let processed_frame_count = encode_raw(CODEC_ID_HEVC, ENC_HEVC_MAIN, 31);
        assert_eq!(processed_frame_count, 300);
    }

    #[test]
    fn test_h264_encode() {
        let processed_frame_count = encode_raw(CODEC_ID_H264, ENC_H264_BASELINE, 31);
        assert_eq!(processed_frame_count, 300);
    }
}
