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

/*---- encoder ----*/
pub mod xlnx_encoder;
pub use xlnx_encoder::*;

pub mod xlnx_enc_props;
pub use xlnx_enc_props::*;

pub mod xlnx_enc_utils;
pub use xlnx_enc_utils::*;

/*---- error ----*/
pub mod xlnx_error;
pub use xlnx_error::*;

const XCLBIN_FILENAME: &[u8] = b"/opt/xilinx/xcdr/xclbins/transcode.xclbin\0";

/// initalizes all devices on system with the default xclbin_name
///
pub fn xlnx_init_all_devices() -> Result<i32, simple_error::SimpleError> {
    let mut xclbin_params = Vec::new();
    let device_count =  unsafe { xclProbe() } as i32;
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

    Ok(device_count)
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
    use crate::{sys::*, xlnx_init_all_devices};
    use std::sync::Once;

    use std::{fs::File, io::Read};
    pub const RAW_YUV_P_FILE_PATH: &str = "src/testdata/smptebars.yuv";
    static INIT: Once = Once::new();

    pub fn initialize() {
        INIT.call_once(|| {
            xlnx_init_all_devices().unwrap();
        });
    }

    /// read raw frames from raw yuv420p file.
    pub fn read_raw_yuv_420_p(file_path: &str, frame_props: &mut XmaFrameProperties, frame_count: usize) -> (Vec<*mut XmaFrame>, Vec<Vec<u8>>, Vec<Vec<u8>>) {
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
}
