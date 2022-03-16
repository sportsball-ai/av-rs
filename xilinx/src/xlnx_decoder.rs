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
        // reserve the required decoding resources and assign reserve ID
        xlnx_reserve_dec_resource(xlnx_dec_ctx)?;

        let dec_session = xlnx_create_dec_session(xma_dec_props, xlnx_dec_ctx)?;

        let mut frame_props: XmaFrameProperties = Default::default();
        let ret = unsafe { xma_dec_session_get_properties(dec_session, &mut frame_props) };
        if ret != XMA_SUCCESS as i32 {
            bail!("unable to get frame properties from decoder session")
        }

        let in_buf: XmaDataBuffer = Default::default();

        let out_frame = unsafe { xma_frame_alloc(&mut frame_props, false) };

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
