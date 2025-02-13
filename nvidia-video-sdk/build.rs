use std::{env, path::PathBuf};

use which::which;

fn main() {
    let cuda_folder = which("nvcc").unwrap().parent().unwrap().to_path_buf();
    let bindings = bindgen::builder()
        .header("src/vendored-headers/nvcuvid.h")
        .header("src/vendored-headers/cuviddec.h")
        .header("src/vendored-headers/nvEncodeAPI.h")
        .clang_arg(format!("-I{}", cuda_folder.join("include").display()))
        .allowlist_item("nv*")
        .allowlist_item("cuda*")
        .allowlist_item("CU*")
        .allowlist_item("cuvid*")
        .rustified_non_exhaustive_enum("*")
        .generate()
        .unwrap();
    bindings
        .write_to_file(PathBuf::from(env::var("OUT_DIR").unwrap()).join("video_sdk_bindings.rs"))
        .unwrap();
}
