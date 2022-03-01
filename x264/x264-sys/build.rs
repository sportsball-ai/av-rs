extern crate bindgen;

use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rustc-link-lib=x264");

    cc::Build::new().file("src/lib.c").compile("x264");

    let bindings = bindgen::Builder::default();

    // the "whilelist_" functions have been renamed in newer bindgen versions, but we use the old
    // names for wider compatibility
    #[allow(deprecated)]
    let bindings = bindings
        .header("src/lib.h")
        .whitelist_function("x264_.+")
        .whitelist_type("x264_.+")
        .whitelist_var("X264_.+")
        .generate()
        .expect("unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
}
