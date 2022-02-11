pub mod sys;
pub use sys::*;

/*---- decoder ----*/
pub mod xlnx_decoder;
pub use xlnx_decoder::*;

pub mod xlnx_dec_utils;
pub use xlnx_dec_utils::*;

pub mod xlnx_dec_props;
pub use xlnx_dec_props::*;

/*---- error ----*/
pub mod xlnx_error;
pub use xlnx_error::*;

const XCLBIN_FILENAME: &str = "/opt/xilinx/xcdr/xclbins/transcode.xclbin";

/// initalized the number of devices specified with the default xclbin_name
///
/// @decice_count: The number of devices available in the system.
pub fn xlnx_init_all_devices(device_count: i32) -> Result<(), simple_error::SimpleError> {
    let mut xclbin_params = Vec::new();

    let fname = std::ffi::CString::new(XCLBIN_FILENAME).expect("CString::new failed");
    let fname_ptr = fname.into_raw();

    for i in 0..device_count {
        xclbin_params.push(XmaXclbinParameter {
            xclbin_name: fname_ptr,
            device_id: i,
        });
    }

    let ret = unsafe { xma_initialize(xclbin_params.as_mut_ptr(), device_count) };
    if ret as u32 != XMA_SUCCESS {
        simple_error::bail!("Xma initalization failed")
    }

    Ok(())
}

pub fn xrm_precision_1000000_bitmask(val: i32) -> i32 {
    return val << 8;
}

// performs a strcpy from string literal to existing output array. ensures null termination.
pub fn strcpy_to_arr_i8(buff: &mut [i8], buff_len: usize, in_str: &str) -> Result<(), simple_error::SimpleError> {
    //check string length is smaller than buffer size. requires one byte for null termination
    if in_str.len() > buff_len - 1 {
        simple_error::bail!("Input str exceeds output buffer size")
    }

    for (i, c) in in_str.chars().enumerate() {
        buff[i] = c as i8;
    }
    buff[in_str.len()] = '\0' as i8;
    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        let result = 2 + 2;
        assert_eq!(result, 4);
    }
}
