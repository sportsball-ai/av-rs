fn main() {
    #[cfg(target_os = "macos")]
    {
        use std::env;
        use std::path::PathBuf;

        println!("cargo:rustc-link-lib=framework=CoreVideo");

        let sdk_root = "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";

        let bindings = bindgen::Builder::default()
            .clang_arg(format!("-isysroot{}", sdk_root))
            .header("src/lib.hpp")
            .allowlist_function("CVImageBuffer.+")
            .allowlist_function("CVPixelBuffer.+")
            .allowlist_var("kCVPixelBufferLock_ReadOnly")
            // See: https://github.com/rust-lang/rust-bindgen/issues/1671
            .size_t_is_usize(true)
            .generate()
            .expect("unable to generate bindings");

        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
        bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
    }
}
