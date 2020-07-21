use std::env;
use std::path::PathBuf;

fn main() {
    #[cfg(target_os = "macos")]
    {
        println!("cargo:rustc-link-lib=framework=CoreVideo");

        let sdk_root = "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";

        let bindings = bindgen::Builder::default()
            .clang_arg(format!("-isysroot{}", sdk_root))
            .header("src/lib.hpp")
            .whitelist_function("CVImageBuffer.+")
            .generate()
            .expect("unable to generate bindings");

        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
        bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
    }
}
