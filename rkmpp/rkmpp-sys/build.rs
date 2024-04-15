extern crate bindgen;

use std::env;
use std::path::PathBuf;

fn main() {
    let bindings = bindgen::Builder::default()
        .clang_arg("-Ivendor/mpp/include")
        .header("src/lib.h")
        .allowlist_function("mpp_.+")
        .allowlist_type("MppEnc.+")
        .dynamic_library_name("rockchip_mpp")
        .generate()
        .expect("unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
}
