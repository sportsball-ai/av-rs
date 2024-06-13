extern crate bindgen;

use std::env;
use std::path::PathBuf;

fn main() {
    let lib = pkg_config::probe_library("x264").unwrap();

    // pkg-config emits cargo: directives for linking automatically.

    let mut wrapper = cc::Build::new();

    // Pass include paths on to cc. It'd be nice if pkg-config allowed fetching CFLAGS and
    // passing that on; see <https://github.com/alexcrichton/pkg-config-rs/issues/43>. But
    // the include paths are likely all that's included/significant for compilation.
    for p in &lib.include_paths {
        wrapper.include(p);
    }

    wrapper.file("src/lib.c").compile("x264-sys");

    let bindings = bindgen::Builder::default()
        // As with cc, pass along include paths to clang.
        .clang_args(lib.include_paths.iter().map(|p| format!("-I{}", p.to_str().expect("path is valid UTF-8"))))
        .header("src/lib.h")
        .allowlist_function("x264_.+")
        .allowlist_type("x264_.+")
        .allowlist_var("X264_.+")
        .generate()
        .expect("unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
}
