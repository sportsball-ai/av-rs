fn main() {
    #[cfg(target_os = "macos")]
    {
        use std::env;
        use std::path::PathBuf;

        println!("cargo:rustc-link-lib=framework=CoreMedia");

        let sdk_root = "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";

        // the "whilelist_" functions have been renamed in newer bindgen versions, but we use the
        // old names for wider compatibility
        #[allow(deprecated)]
        let bindings = bindgen::Builder::default()
            .clang_arg(format!("-isysroot{}", sdk_root))
            .header("src/lib.hpp")
            .allowlist_function("CMVideoFormatDescription.+")
            .allowlist_function("CMBlockBuffer.+")
            .allowlist_function("CMSampleBuffer.+")
            .allowlist_var("kCFAllocator.+")
            .allowlist_var("kCMVideoCodecType_.+")
            // See: https://github.com/rust-lang/rust-bindgen/issues/1671
            .size_t_is_usize(true)
            .generate()
            .expect("unable to generate bindings");

        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
        bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
    }
}
