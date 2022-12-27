use std::env;
use std::path::PathBuf;

fn main() {
    let library = pkg_config::probe_library("kvazaar").unwrap();
    let bindings = bindgen::Builder::default();

    // the "whilelist_" functions have been renamed in newer bindgen versions, but we use the old
    // names for wider compatibility
    #[allow(deprecated)]
    let bindings = bindings
        .clang_args(library.include_paths.iter().map(|p| format!("-I{}", p.to_str().expect("path is valid UTF-8"))))
        .header("src/lib.hpp")
        .allowlist_function("kvz_.+")
        .allowlist_type("kvz_.+")
        .generate()
        .expect("unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
}
