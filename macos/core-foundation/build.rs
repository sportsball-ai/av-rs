fn main() {
    #[cfg(target_os = "macos")]
    {
        use std::env;
        use std::path::PathBuf;

        println!("cargo:rustc-link-lib=framework=CoreFoundation");

        let sdk_root = "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";

        let bindings = bindgen::Builder::default()
            .clang_arg(format!("-isysroot{}", sdk_root))
            .header("src/lib.hpp")
            .whitelist_function("CF.+")
            .whitelist_var("kCFString.+")
            .whitelist_type("CFStringBuiltInEncodings")
            .whitelist_type("OSStatus")
            .prepend_enum_name(false)
            .generate()
            .expect("unable to generate bindings");

        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
        bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
    }
}
