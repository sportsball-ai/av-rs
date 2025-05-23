fn main() {
    #[cfg(target_os = "linux")]
    {
        use std::{env, fs::create_dir_all, path::PathBuf, process::Command};

        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

        println!("cargo:rerun-if-changed=vendor/libxcoder_logan");
        let libxcoder_path = out_path.join("libxcoder_logan");
        Command::new("cp")
            .args(["-r", "vendor/libxcoder_logan", libxcoder_path.to_str().unwrap()])
            .status()
            .unwrap();

        let source_path = libxcoder_path.join("source");

        let build_path = libxcoder_path.join("build");
        create_dir_all(&build_path).unwrap();

        Command::new("bash")
            .env("XCODER_OBJS", &build_path)
            .env("XCODER_TRACELOG_TIMESTAMPS", "NO")
            .env("XCODER_OLD_NVME_DRIVER", "NO")
            .env("XCODER_IO_RW", "YES")
            .env("XCODER_WIN32", "NO")
            .env("XCODER_WIN_NVME_CUSTOM", "NO")
            .env("XCODER_ENCODER_SYNC_QUERY", "NO")
            .env("XCODER_SELF_KILL_ERR", "NO")
            .env("XCODER_LINUX_CUSTOM_DRIVER", "NO")
            .env("XCODER_LINUX_VIRT_IO_DRIVER", "NO")
            .env("XCODER_LATENCY_DISPLAY", "NO")
            .env("XCODER_SIGNATURE_FILE", "NO")
            .env("XCODER_DUMP_DATA", "NO")
            .env("XCODER_LIBDIR", "NO")
            .env("XCODER_BINDIR", "NO")
            .env("XCODER_INCLUDEDIR", "NO")
            .env("XCODER_SHAREDDIR", "NO")
            .args(["vendor/libxcoder_logan/auto/auto_headers.sh"])
            .status()
            .unwrap();

        let c_source_files = [
            "ni_log_logan.c",
            "ni_nvme_logan.c",
            "ni_device_api_priv_logan.c",
            "ni_device_api_logan.c",
            "ni_util_logan.c",
        ];

        cc::Build::new()
            .files(c_source_files.iter().map(|name| source_path.join(name)))
            .warnings(false)
            .flag("-fPIC")
            .flag("-Werror")
            .flag("-Wno-unused-command-line-argument")
            .flag("-Wno-format-truncation")
            .flag("-Wno-stringop-overflow")
            .flag("-std=gnu99")
            .flag("-DLIBXCODER_OBJS_BUILD")
            .flag("-include")
            .flag("src/header.h")
            .compile("xcoder-logan-c-sys");

        // Our own logging bridge
        println!("cargo:rerun-if-changed=../../logging_shim.c");
        cc::Build::new()
            .file("../../logging_shim.c")
            .define("LOGAN", None)
            .include(source_path.to_str().unwrap())
            .flag("-fPIC")
            .flag("-Wno-unused-command-line-argument")
            .flag("-std=gnu99")
            .flag("-DLIBXCODER_OBJS_BUILD")
            .compile("xcoder-logging-shim");

        let cpp_source_files = ["ni_rsrc_priv_logan.cpp", "ni_rsrc_api_logan.cpp"];

        cc::Build::new()
            .files(cpp_source_files.iter().map(|name| source_path.join(name)))
            .warnings(false)
            .flag("-fPIC")
            .flag("-Werror")
            .flag("-Wno-unused-command-line-argument")
            .flag("-Wno-format-overflow")
            .flag("-Wno-stringop-overflow")
            .flag("-DLIBXCODER_OBJS_BUILD")
            .flag("-include")
            .flag("src/header.h")
            .compile("xcoder-logan-cpp-sys");

        let bindings = bindgen::Builder::default()
            .generate_comments(false)
            .header("src/lib.hpp")
            .allowlist_function("ni_.+")
            .allowlist_type("ni_.+")
            .allowlist_var("NI_.+")
            .allowlist_var("LOGAN_.+")
            // bindgen 0.60.0 broke layout tests: https://github.com/rust-lang/rust-bindgen/issues/2218
            // 0.60.1 claims to have fixed the issue, but does not.
            .layout_tests(false)
            .clang_arg(format!("-I{}", source_path.to_str().unwrap()))
            .generate()
            .expect("unable to generate bindings");

        bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
    }
}
