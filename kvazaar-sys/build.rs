extern crate bindgen;

use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rustc-link-lib=kvazaar");

    let bindings = bindgen::Builder::default();

    let bindings = bindings
        .header("src/lib.hpp")
        .whitelist_function("kvz_.+")
        .whitelist_type("kvz_.+")
        .generate()
        .expect("unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
}
