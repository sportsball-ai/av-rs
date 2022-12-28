#[cfg(target_os = "linux")]
use std::env;
extern crate bindgen;
#[cfg(target_os = "linux")]
use std::path::PathBuf;

fn main() {
    //builds with xilinx libraries for vt1 instances if paths are provided by XILINX_LIBS_PATH env var
    #[cfg(target_os = "linux")]
    {
        env::set_var("LD_LIBRARY_PATH", "/opt/xilinx/xrm/lib:/opt/xilinx/xrt/lib");
        println!("cargo:rustc-link-search=native=/opt/xilinx/xrm/lib");
        println!("cargo:rustc-link-search=native=/opt/xilinx/xrt/lib");
        println!("cargo:rustc-link-search=native=/opt/xilinx/xvbm/lib");
        println!("cargo:rustc-link-lib=dylib=xma2api");
        println!("cargo:rustc-link-lib=dylib=xma2plugin");
        println!("cargo:rustc-link-lib=static=xvbm");
        println!("cargo:rustc-link-lib=dylib=stdc++");
        println!("cargo:rustc-link-lib=dylib=xrm");
        println!("cargo:rustc-link-lib=dylib=boost_system");
        println!("cargo:rustc-link-lib=dylib=boost_filesystem");
        println!("cargo:rustc-link-lib=dylib=boost_thread");
        println!("cargo:rustc-link-lib=dylib=boost_serialization");
        println!("cargo:rustc-link-lib=dylib=uuid");
        println!("cargo:rustc-link-lib=dylib=dl");
        println!("cargo:rustc-link-lib=dylib=xrt_core");

        let bindings = bindgen::Builder::default();
        #[allow(deprecated)]
        let bindings = bindings
            .header("src/bindings.h")
            .clang_arg("-I/opt/xilinx/xrt/include/xma2")
            .clang_arg("-I/opt/xilinx/xrt/include")
            .clang_arg("-I/opt/xilinx/xvbm/include")
            .clang_arg("-I/opt/xilinx/xrm/include")
            .allowlist_function("xrm.*")
            .allowlist_function("xvbm.*")
            .allowlist_function("xma_.*")
            .allowlist_function("xclProbe.*")
            .allowlist_type("xma.*")
            .allowlist_type("xvbm.*")
            .allowlist_type("xrm.*")
            .allowlist_var("XRM_.*")
            .allowlist_var("XVBM_.*")
            .allowlist_var("XMA_.*")
            .derive_default(true)
            .generate()
            .expect("unable to generate bindings");

        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
        bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
    }
}
