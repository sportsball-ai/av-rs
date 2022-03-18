pub mod sys;
pub use sys::*;

/*---- decoder ----*/
pub mod xlnx_decoder;
pub use xlnx_decoder::*;

pub mod xlnx_dec_utils;
pub use xlnx_dec_utils::*;

pub mod xlnx_dec_props;
pub use xlnx_dec_props::*;

/*---- scaler ----*/
pub mod xlnx_scaler;
pub use xlnx_scaler::*;

pub mod xlnx_scal_props;
pub use xlnx_scal_props::*;

pub mod xlnx_scal_utils;
pub use xlnx_scal_utils::*;

/*---- error ----*/
pub mod xlnx_error;
pub use xlnx_error::*;

const XCLBIN_FILENAME: &[u8] = b"/opt/xilinx/xcdr/xclbins/transcode.xclbin\0";

/// initalized the number of devices specified with the default xclbin_name
///
/// @decice_count: The number of devices available in the system.
pub fn xlnx_init_all_devices(device_count: i32) -> Result<(), simple_error::SimpleError> {
    let mut xclbin_params = Vec::new();

    for i in 0..device_count {
        xclbin_params.push(XmaXclbinParameter {
            xclbin_name: XCLBIN_FILENAME.as_ptr() as *mut i8,
            device_id: i,
        });
    }

    let ret = unsafe { xma_initialize(xclbin_params.as_mut_ptr(), device_count) };
    if ret as u32 != XMA_SUCCESS {
        simple_error::bail!("xma initalization failed")
    }

    Ok(())
}

pub(crate) fn xrm_precision_1000000_bitmask(val: i32) -> i32 {
    val << 8
}

// performs a strcpy from string literal to existing output array. ensures null termination.
pub(crate) fn strcpy_to_arr_i8(buf: &mut [i8], in_str: &str) -> Result<(), simple_error::SimpleError> {
    //check string length is smaller than buffer size. requires one byte for null termination
    if in_str.len() > buf.len() - 1 {
        simple_error::bail!("input str exceeds output buffer size")
    }

    let src: Vec<i8> = in_str.as_bytes().iter().map(|c| *c as i8).collect();
    buf[..src.len()].copy_from_slice(&src[..]);
    buf[in_str.len()] = 0;

    Ok(())
}

#[cfg(test)]
pub mod tests {
    use crate::{sys::*, xlnx_decoder::tests::*, xlnx_error::*, xlnx_init_all_devices, xlnx_scal_props::*, xlnx_scal_utils::*, xlnx_scaler::*};
    use std::{fs::File, io::Read};
    const H265_TEST_FILE_PATH: &str = "src/testdata/hvc1.1.6.L150.90.h265";
    const H264_TEST_FILE_PATH: &str = "src/testdata/smptebars.h264";
    const RAW_YUV_P_FILE_PATH: &str = "src/testdata/smptebars.yuv";

    use std::sync::Once;

    static INIT: Once = Once::new();

    pub fn initialize() {
        INIT.call_once(|| {
            xlnx_init_all_devices(2).unwrap();
        });
    }

    /// read raw frames from raw yuv420p file.
    fn read_raw_yuv_420_p(file_path: &str, frame_props: &mut XmaFrameProperties, frame_count: usize) -> (Vec<*mut XmaFrame>, Vec<Vec<u8>>, Vec<Vec<u8>>) {
        let mut f = File::open(file_path).unwrap();
        let frame_size_y = (frame_props.width * frame_props.height) as usize;
        let frame_size_uv = frame_size_y / 2;

        // Do this so that the memory persists..
        let mut y_components = vec![vec![0u8; frame_size_y]; frame_count];
        let mut uv_components = vec![vec![0u8; frame_size_uv]; frame_count];

        let mut frames: Vec<*mut XmaFrame> = Vec::new();
        unsafe {
            for i in 0..frame_count {
                let frame = xma_frame_alloc(frame_props, false);

                for j in 0..2 {
                    (*frame).data[j].refcount = 1;
                    (*frame).data[j].buffer_type = XmaBufferType_XMA_HOST_BUFFER_TYPE;
                    (*frame).data[j].is_clone = true;
                }

                // read Y component
                match f.read_exact(&mut y_components[i]) {
                    Ok(_) => {
                        (*frame).data[0].buffer = y_components[i].as_mut_ptr() as *mut std::ffi::c_void;
                    }
                    Err(e) => {
                        xma_frame_free(frame);
                        if let std::io::ErrorKind::UnexpectedEof = e.kind() {
                            break;
                        } else {
                            panic!("{}", e);
                        }
                    }
                }
                // extract UV components
                let mut chroma_data = vec![0u8; frame_size_uv];
                match f.read_exact(&mut chroma_data) {
                    Ok(_) => {
                        let frame_u = &chroma_data[0..frame_size_uv / 2];
                        let frame_v = &chroma_data[frame_size_uv / 2..];
                        // interlace uv components for raw frame
                        for j in 0..frame_size_uv / 2 {
                            uv_components[i][2 * j] = frame_u[j];
                            uv_components[i][2 * j + 1] = frame_v[j];
                        }
                        (*frame).data[1].buffer = uv_components[i].as_mut_ptr() as *mut std::ffi::c_void;
                    }
                    Err(e) => {
                        xma_frame_free(frame);
                        if let std::io::ErrorKind::UnexpectedEof = e.kind() {
                            break;
                        } else {
                            panic!("{}", e);
                        }
                    }
                }
                frames.push(frame);
            }
        }

        (frames, y_components, uv_components)
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
        let mut scal_props = Box::new(XlnxScalerProperties {
            in_width: 1280,
            in_height: 720,
            fr_num: 1,
            fr_den: 25,
            nb_outputs: 3,
            out_width: [1280, 852, 640, 0, 0, 0, 0, 0],
            out_height: [720, 480, 360, 0, 0, 0, 0, 0],
            enable_pipeline: 0,
            log_level: 3,
            latency_logging: 1,
        });

        let scal_params: [XmaParameter; MAX_SCAL_PARAMS] = Default::default();
        let mut scal_params = Box::new(scal_params);

        let mut xma_scal_props = xlnx_create_xma_scal_props(&mut scal_props, &mut scal_params).unwrap();

        let xrm_ctx = unsafe { xrmCreateContext(XRM_API_VERSION_1) };

        let cu_res: xrmCuResource = Default::default();

        let mut xlnx_scal_ctx = XlnxScalerXrmCtx {
            xrm_reserve_id: 0,
            device_id: -1,
            scal_load: xlnx_calc_scal_load(xrm_ctx, &mut *xma_scal_props).unwrap(),
            scal_res_in_use: false,
            xrm_ctx,
            num_outputs: 3,
            cu_res,
        };

        // create xlnx scaler
        let mut scaler = XlnxScaler::new(&mut *xma_scal_props, &mut xlnx_scal_ctx).unwrap();

        let mut processed_frame_count = 0;

        for xma_frame in xma_frames {
            match scaler.process_frame(xma_frame) {
                Ok(_) => {
                    // successfully scaled frame.
                    processed_frame_count += 1;
                    // clear xvbm buffers for each return frame
                    for i in 0..xma_scal_props.num_outputs as usize {
                        unsafe {
                            let handle: XvbmBufferHandle = (*scaler.out_frame_list[i]).data[0].buffer;
                            xvbm_buffer_pool_entry_free(handle);
                        }
                    }
                }
                Err(e) => match e.err {
                    XlnxErrorType::XlnxSendMoreData => {}
                    _ => panic!("scalar processing has failed with error {:?}", e),
                },
            };
            if !xma_frame.is_null() {
                unsafe { xma_frame_free(xma_frame) };
            }
        }

        assert_eq!(processed_frame_count, 300);
    }
}
