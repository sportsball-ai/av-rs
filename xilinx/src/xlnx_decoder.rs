use crate::sys::*;
use crate::xlnx_dec_utils::*;
use crate::XlnxError;
use simple_error::{bail, SimpleError};

pub struct XlnxDecoder {
    pub dec_session: *mut XmaDecoderSession,
    pub frame_props: XmaFrameProperties,
    in_buf: XmaDataBuffer,
    pub out_frame: *mut XmaFrame,
    pub flush_sent: bool,
}

impl XlnxDecoder {
    pub fn new(xma_dec_props: &mut XmaDecoderProperties, xlnx_dec_ctx: &mut XlnxDecoderXrmCtx) -> Result<Self, SimpleError> {
        let dec_session = xlnx_create_dec_session(xma_dec_props, xlnx_dec_ctx)?;

        let mut frame_props: XmaFrameProperties = Default::default();
        let ret = unsafe { xma_dec_session_get_properties(dec_session, &mut frame_props) };
        if ret != XMA_SUCCESS as i32 {
            bail!("unable to get frame properties from decoder session")
        }

        let in_buf: XmaDataBuffer = Default::default();

        let out_frame = unsafe { xma_frame_alloc(&mut frame_props, true) };

        // Loop through the planes. no buffer shouold be allocated yet.
        // Since this will be used in a pipeline, xvbm will allocate the buffers.
        // So we need to specify to use device buffers.
        unsafe {
            for i in 0..2 {
                (*out_frame).data[i].buffer_type = XmaBufferType_XMA_DEVICE_BUFFER_TYPE;
                (*out_frame).data[i].is_clone = true;
            }
        }

        Ok(Self {
            dec_session,
            frame_props,
            in_buf,
            out_frame,
            flush_sent: false,
        })
    }

    /// Sends data to xilinx decoder using xma plugin.
    ///
    /// @buf: buffer of input data.
    /// @size: size of input data.
    pub fn xlnx_dec_send_pkt(&mut self, buf: &mut [u8], pts: i32) -> Result<(), XlnxError> {
        let mut data_used = 0;
        let mut index = 0;
        let mut ret;

        let size = buf.len();
        while index < size {
            unsafe {
                self.in_buf.data.buffer = buf.as_mut_ptr() as *mut _ as *mut std::ffi::c_void;
                self.in_buf.alloc_size = buf.len() as i32;
                self.in_buf.pts = pts;
                self.in_buf.is_eof = 0;

                ret = xma_dec_session_send_data(self.dec_session, &mut self.in_buf, &mut data_used);
            }

            if ret != XMA_SUCCESS as i32 {
                return Err(XlnxError::new(ret, Some("error sending packet to decoder".to_string())));
            }

            index += data_used as usize;
        }
        Ok(())
    }

    /// Receives decoded frame into internal out_frame object
    pub fn xlnx_dec_recv_frame(&mut self) -> Result<(), XlnxError> {
        let ret = unsafe { xma_dec_session_recv_frame(self.dec_session, self.out_frame) };
        if ret != XMA_SUCCESS as i32 {
            return Err(XlnxError::new(ret, Some("error receiving decoded frame from decoder".to_string())));
        }
        Ok(())
    }

    /// Sends a flush frame to the decoder with eof flag to start decoder flush
    pub fn xlnx_send_flush_frame(&mut self) -> Result<(), XlnxError> {
        let mut buffer: XmaDataBuffer = Default::default();
        let mut data_used = 0;

        //fill empty buffer data
        buffer.data.buffer = std::ptr::null_mut();
        buffer.alloc_size = 0;
        buffer.is_eof = 1;

        let ret = unsafe { xma_dec_session_send_data(self.dec_session, &mut buffer, &mut data_used) };
        if ret != XMA_SUCCESS as i32 {
            return Err(XlnxError::new(ret, Some("error sending flush frame".to_string())));
        }
        Ok(())
    }

    /// Sends a null frame to the decoder
    pub fn xlnx_dec_send_null_frame(&mut self) -> Result<(), XlnxError> {
        let mut buffer: XmaDataBuffer = Default::default();
        let mut data_used = 0;

        //fill empty buffer data
        buffer.data.buffer = std::ptr::null_mut();
        buffer.alloc_size = 0;
        buffer.is_eof = 0;
        buffer.pts = -1;

        let ret = unsafe { xma_dec_session_send_data(self.dec_session, &mut buffer, &mut data_used) };
        if ret != XMA_SUCCESS as i32 {
            return Err(XlnxError::new(ret, Some("error sending null frame".to_string())));
        }
        self.flush_sent = true;
        Ok(())
    }
}

impl Drop for XlnxDecoder {
    fn drop(&mut self) {
        unsafe {
            if !self.dec_session.is_null() {
                xma_dec_session_destroy(self.dec_session);
            }
            if !self.out_frame.is_null() {
                xma_frame_free(self.out_frame);
            }
        }
    }
}

#[cfg(test)]
mod decoder_tests {
    use crate::{tests::*, xlnx_dec_props::*, xlnx_dec_utils::*, xlnx_decoder::*, xlnx_error::*};
    use std::{fs::File, io::Read};

    const H265_TEST_FILE_PATH: &str = "src/testdata/hvc1.1.6.L150.90.h265";
    const H264_TEST_FILE_PATH: &str = "src/testdata/smptebars.h264";

    pub fn decode_file(file_path: &str, codec_type: u32, profile: u32, level: u32) -> (i32, i32) {
        initialize();

        // create a Xlnx decoder's context
        let dec_props = XlnxDecoderProperties {
            width: 1280,
            height: 720,
            bitdepth: 8,
            codec_type,
            low_latency: 0,
            entropy_buffers_count: 2,
            zero_copy: 1,
            profile,
            level,
            chroma_mode: 420,
            scan_type: 1,
            latency_logging: 1,
            splitbuff_mode: 0,
            framerate: XmaFraction { numerator: 25, denominator: 1 },
        };
        let frame_duration_secs = dec_props.framerate.denominator as f64 / dec_props.framerate.numerator as f64;

        let mut xma_dec_props = XlnxXmaDecoderProperties::from(dec_props);

        let xrm_ctx = unsafe { xrmCreateContext(XRM_API_VERSION_1) };

        let cu_list_res: xrmCuListResource = Default::default();

        let mut xlnx_dec_ctx = XlnxDecoderXrmCtx {
            xrm_reserve_id: 0,
            device_id: -1,
            dec_load: xlnx_calc_dec_load(xrm_ctx, xma_dec_props.as_mut()).unwrap(),
            decode_res_in_use: false,
            xrm_ctx,
            cu_list_res,
        };

        // create Xlnx decoder
        let mut decoder = XlnxDecoder::new(xma_dec_props.as_mut(), &mut xlnx_dec_ctx).unwrap();
        let mut frames_decoded = 0;
        let mut packets_sent = 0;

        //open file and read data nalus
        let mut f = File::open(file_path).unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();

        let nalus: Vec<_>;
        if codec_type == 0 {
            nalus = h264::iterate_annex_b(&buf).collect();
        } else {
            nalus = h265::iterate_annex_b(&buf).collect();
        }

        for (frame_count, nalu) in nalus.into_iter().enumerate() {
            let pts = (frame_duration_secs * 90000.0 * frame_count as f64) as i32;
            let mut data = vec![0, 0, 0, 1];
            data.extend_from_slice(nalu);
            let data = data.as_mut_slice();

            loop {
                let mut packet_send_success = false;
                match decoder.xlnx_dec_send_pkt(data, pts as i32) {
                    Ok(_) => {
                        packets_sent += 1;
                        packet_send_success = true;
                    }
                    Err(e) => {
                        if let XlnxErrorType::XlnxErr = e.err {
                            panic!("error sending packet to xilinx decoder: {}", e.message)
                        }
                    }
                };
                loop {
                    match decoder.xlnx_dec_recv_frame() {
                        Ok(_) => {
                            // the frame was decoded. do nothing yet.
                            frames_decoded += 1;
                            // throw away the frame. Clear XVBM buffer
                            unsafe {
                                let handle: XvbmBufferHandle = (*decoder.out_frame).data[0].buffer;
                                xvbm_buffer_pool_entry_free(handle);
                            }
                        }
                        Err(e) => {
                            match e.err {
                                XlnxErrorType::XlnxTryAgain => break,
                                _ => panic!("error receiving frame from decoder"),
                            };
                        }
                    };
                }
                if packet_send_success {
                    break;
                }
            }
        }
        // flush decoder
        decoder.xlnx_send_flush_frame().unwrap();
        decoder.flush_sent = true;

        loop {
            decoder.xlnx_dec_send_null_frame().unwrap();
            let mut finished = false;
            loop {
                match decoder.xlnx_dec_recv_frame() {
                    Ok(_) => {
                        frames_decoded += 1;
                        // the frame was successfully decoded.
                    }
                    Err(e) => match e.err {
                        XlnxErrorType::XlnxEOS => {
                            finished = true;
                            break;
                        }
                        XlnxErrorType::XlnxTryAgain => break,
                        _ => panic!("error receiving frame from decoder while flushing. Got error"),
                    },
                }
            }
            if finished {
                break;
            }
        }
        println!("finished decoding. num packets sent: {}, num frames recieved: {}", packets_sent, frames_decoded);
        // this has already been freed by xvbm_buffer_pool_entry_free. Not safe.
        // if we don't set it to null, the decoder will attempt to free it again.
        (packets_sent, frames_decoded)
    }

    #[test]
    fn test_hevc_decode() {
        let (packets_sent, frames_decoded) = decode_file(H265_TEST_FILE_PATH, 1, 1, 93);
        assert_eq!(packets_sent, 771); // there should be 739 frames but 771 nal units to send to the decoder
        assert_eq!(frames_decoded, 739);
    }

    #[test]
    fn test_h264_decode() {
        let (packets_sent, frames_decoded) = decode_file(H264_TEST_FILE_PATH, 0, 100, 31);
        assert_eq!(packets_sent, 321); // there are 300 frames but 321 nal units to send to the decoder
        assert_eq!(frames_decoded, 300);
    }
}
