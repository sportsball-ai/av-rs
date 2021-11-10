fn main() {
    #[cfg(target_os = "macos")]
    {
        use std::env;
        use std::path::PathBuf;

        println!("cargo:rustc-link-lib=framework=AudioToolbox");

        let sdk_root = "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";

        // the "whilelist_" functions have been renamed in newer bindgen versions, but we use the
        // old names for wider compatibility
        #[allow(deprecated)]
        let bindings = bindgen::Builder::default()
            .clang_arg(format!("-isysroot{}", sdk_root))
            .header("src/lib.hpp")
            .whitelist_function("AudioQueue.+")
            .whitelist_var("kAudioQueueParam_.+")
            .generate()
            .expect("unable to generate bindings");

        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
        bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
    }
}
