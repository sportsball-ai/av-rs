use crate::sys::*;
use crate::xlnx_dec_utils::*;
use crate::XlnxDecodeError;
use simple_error::{bail, SimpleError};

pub struct XlnxDecoder {
    pub dec_session: *mut XmaDecoderSession,
    pub frame_props: XmaFrameProperties,
    in_buf: *mut XmaDataBuffer,
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
            bail!("Unable to get frame properties from decoder session")
        }

        let in_buf = unsafe { xma_data_buffer_alloc(0, true) };

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
    pub fn xlnx_dec_send_pkt(&mut self, buf: &mut [u8]) -> Result<(), XlnxDecodeError> {
        let mut data_used = 0;
        let mut index = 0;
        let mut ret;

        let size = buf.len();
        while index < size {
            unsafe {
                self.in_buf = xma_data_from_buffer_clone(buf.as_mut_ptr(), size as u64);
                (*self.in_buf).pts = 0; //no need to assign pts on input keep track of this elsewhere

                ret = xma_dec_session_send_data(self.dec_session, self.in_buf, &mut data_used);

                xma_data_buffer_free(self.in_buf);
            }

            if ret != XMA_SUCCESS as i32 {
                return Err(XlnxDecodeError::new(ret, None));
            }

            index += data_used as usize;
        }

        Ok(())
    }

    /// Receives decoded frame into internal out_frame object
    pub fn xlnx_dec_recv_frame(&mut self) -> Result<(), XlnxDecodeError> {
        let ret = unsafe { xma_dec_session_recv_frame(self.dec_session, self.out_frame) };
        if ret != XMA_SUCCESS as i32 {
            return Err(XlnxDecodeError::new(ret, None));
        }
        Ok(())
    }

    /// Sends a null frame to the decoder with eof flag to start decoder flush
    pub fn xlnx_send_flush_frame(&mut self) -> Result<(), XlnxDecodeError> {
        let mut buffer: XmaDataBuffer = Default::default();
        let mut data_used = 0;

        //fill empty buffer data
        buffer.data.buffer = std::ptr::null_mut();
        buffer.alloc_size = 0;
        buffer.is_eof = 1;

        let ret = unsafe { xma_dec_session_send_data(self.dec_session, &mut buffer, &mut data_used) };
        if ret != XMA_SUCCESS as i32 {
            return Err(XlnxDecodeError::new(ret, None));
        }
        Ok(())
    }

    /// Sends a null frame to the decoder with eof flag to start decoder flush
    pub fn xlnx_dec_send_null_frame(&mut self) -> Result<(), XlnxDecodeError> {
        let mut buffer: XmaDataBuffer = Default::default();
        let mut data_used = 0;

        //fill empty buffer data
        buffer.data.buffer = std::ptr::null_mut();
        buffer.alloc_size = 0;
        buffer.is_eof = 0;
        buffer.pts = -1;

        let ret = unsafe { xma_dec_session_send_data(self.dec_session, &mut buffer, &mut data_used) };
        if ret != XMA_SUCCESS as i32 {
            return Err(XlnxDecodeError::new(ret, None));
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
            if !self.in_buf.is_null() {
                xma_data_buffer_free(self.in_buf);
            }
        }
    }
}


#[cfg(test)]
mod tests {
    use crate::{xlnx_init_all_devices, xlnx_dec_props::*, xlnx_decoder::*, xlnx_error::*};
    const H265_TEST_FILE_PATH: &[u8] = b"src/testdata/hvc1.1.6.L150.90.ts";
    const MAX_DEC_PARAMS: usize = 11;

    use std::sync::Once;

    static INIT: Once = Once::new();

    pub fn initialize() {
        INIT.call_once(|| {
            xlnx_init_all_devices(2).unwrap();
        });
    }

    #[test]
    fn test_hevc_decode() {
        initialize();
        let input_path = std::ffi::CString::new(H265_TEST_FILE_PATH).unwrap();
        let mut opts: *mut ffmpeg_sys::AVDictionary = std::ptr::null_mut();
        let mut input_ctx: *mut ffmpeg_sys::AVFormatContext = std::ptr::null_mut();

        // create a Xlnx decoder's context 
        let dec_props = XlnxDecoderProperties {
            width: 1280,
            height: 720,
            bitdepth: 8,
            codec_type: 1,
            low_latency: 0,
            entropy_buffers_count: 2,
            zero_copy: 1,
            profile: 1,
            level: 93,
            chroma_mode: 420,
            scan_type: 1,
            latency_logging: 1,
            splitbuff_mode: 0,
            framerate: XmaFraction { numerator: 1, denominator: 25 },
        };
        let mut dec_props = Box::new(dec_props);

        let dec_params: [XmaParameter; MAX_DEC_PARAMS] = Default::default();
        let mut dec_params = Box::new(dec_params);
        xlnx_fill_dec_params(&mut dec_props, &mut dec_params);

        let mut xma_dec_props = xlnx_create_xma_dec_props(&mut dec_props, &mut dec_params).unwrap();

        let xrm_ctx = unsafe { xrmCreateContext(XRM_API_VERSION_1) };

        let cu_list_res: xrmCuListResource = Default::default();

        let mut xlnx_dec_ctx = XlnxDecoderXrmCtx {
            xrm_reserve_id: 0,
            device_id: -1,
            dec_load: xlnx_calc_dec_load(xrm_ctx, &mut *xma_dec_props).unwrap(),
            decode_res_in_use: false,
            xrm_ctx,
            cu_list_res,
        };
        
        
        // create Xlnx decoder 
        let mut decoder = XlnxDecoder::new(&mut *xma_dec_props, &mut xlnx_dec_ctx).unwrap();

        // handle opening the stream with avformat.
        unsafe {
            if ffmpeg_sys::avformat_open_input(&mut input_ctx, input_path.as_ptr(), 0 as _, &mut opts) != 0  {
                ffmpeg_sys::av_dict_free(&mut opts);
                panic!("unable to open test file");
            }
            ffmpeg_sys::av_dict_free(&mut opts);
       

            if ffmpeg_sys::avformat_find_stream_info(input_ctx, 0 as _) < 0 {
                panic!("unable to find stream info");
            }
            let input_streams = std::slice::from_raw_parts((*input_ctx).streams, (*input_ctx).nb_streams as _);

            let mut pkt = std::mem::zeroed::<ffmpeg_sys::AVPacket>();
            let mut frame_count = 0;
            let mut packets_sent = 0;
            loop {
                match ffmpeg_sys::av_read_frame(input_ctx, &mut pkt) {
                    ffmpeg_sys::AVERROR_EOF => {
                        println!("Got AVERROR_EOF");
                        break
                    },
                    err if err < 0 => panic!("Error reading input frame."),
                    _ => {}
                };

                let stream = &input_streams[pkt.stream_index as usize];
                // only try to decode the video stream
                match (*(**stream).codecpar).codec_type {
                    ffmpeg_sys::AVMediaType::AVMEDIA_TYPE_VIDEO => {
                        let data = std::slice::from_raw_parts_mut(pkt.data, pkt.size as usize);
                        match decoder.xlnx_dec_send_pkt(data) {
                            Ok(_) => {
                                packets_sent += 1;
                            },
                            Err(e) => {
                                match e.err {
                                    XlnxDecodeErrorType::XlnxError => panic!("Error sending packet to xilinx decoder: {}", e.message),
                                    _ => {},
                                }
                            }
                        };
                        loop {
                            match decoder.xlnx_dec_recv_frame() {
                                Ok(_) => {
                                    //the frame was decoded. do nothing yet.
                                    frame_count += 1;
                                },
                                Err(e) => {
                                    match e.err {
                                        XlnxDecodeErrorType::XlnxTryAgain => break,
                                        _ => panic!("Error receiving frame from decoder.")
                                    };
                                },
                            };
                        }
                    },
                    _ => {}
                };                
            }
            println!("**** sent {} packets ****", packets_sent);
            println!("**** frames decoded: {} ****", frame_count);
            // flush decoder
            decoder.xlnx_send_flush_frame().unwrap();
            decoder.flush_sent = true;

            loop {
                decoder.xlnx_dec_send_null_frame().unwrap();
                let mut finished = false;
                //println!("flushing");
                loop {
                    match decoder.xlnx_dec_recv_frame() {
                        Ok(_) => {
                            //the frame was decoded. do nothing yet.
                        },
                        Err(e) => {
                            match e.err {
                                XlnxDecodeErrorType::XlnxEOS => {
                                    finished = true;
                                    break;
                                },
                                XlnxDecodeErrorType::XlnxTryAgain => break,
                                XlnxDecodeErrorType::XlnxError => panic!("Error receiving frame from decoder while flushing. Got Error"),
                            }
                        },
                    }
                }
                if finished {
                    break
                }
            }
        }
    }

    // #[test]
    // fn test_h264_decode() {
        
    //     todo!();
    // }
}