fn main() {
    #[cfg(target_os = "linux")]
    {
        use std::{env, fs::create_dir_all, path::PathBuf, process::Command};

        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

        println!("cargo:rerun-if-changed=vendor/libxcoder");
        let libxcoder_path = out_path.join("libxcoder");
        Command::new("cp")
            .args(["-r", "vendor/libxcoder", libxcoder_path.to_str().unwrap()])
            .status()
            .unwrap();

        let source_path = libxcoder_path.join("source");

        let build_path = libxcoder_path.join("build");
        create_dir_all(&build_path).unwrap();

        Command::new("bash")
            .env("XCODER_OBJS", &build_path)
            .env("XCODER_DUMP_DATA", "NO")
            .env("XCODER_TRACELOG_TIMESTAMPS", "NO")
            .env("XCODER_WIN_NVME_CUSTOM", "NO")
            .env("XCODER_SELF_KILL_ERR", "NO")
            .env("XCODER_LATENCY_DISPLAY", "NO")
            .env("XCODER_SSIM_INFO_LEVEL_LOGGING", "NO")
            .env("XCODER_LINUX_VIRT_IO_DRIVER", "NO")
            .env("XCODER_DISABLE_BACKTRACE_PRINT", "NO")
            .args(["vendor/libxcoder/auto/auto_headers.sh"])
            .status()
            .unwrap();

        let c_source_files = [
            "ni_nvme.c",
            "ni_device_api_priv.c",
            "ni_device_api.c",
            "ni_util.c",
            "ni_log.c",
            "ni_av_codec.c",
            "ni_bitstream.c",
        ];

        cc::Build::new()
            .files(c_source_files.iter().map(|name| source_path.join(name)))
            .warnings(false)
            .flag("-fPIC")
            .flag("-Wno-unused-command-line-argument")
            .flag("-std=gnu99")
            .flag("-DLIBXCODER_OBJS_BUILD")
            .compile("xcoder-c-sys");

        // Our own logging bridge
        println!("cargo:rerun-if-changed=../../logging_shim.c");
        cc::Build::new()
            .file("../../logging_shim.c")
            .include(source_path.to_str().unwrap())
            .flag("-fPIC")
            .flag("-Wno-unused-command-line-argument")
            .flag("-std=gnu99")
            .flag("-DLIBXCODER_OBJS_BUILD")
            .compile("xcoder-logging-shim");

        let cpp_source_files = ["ni_rsrc_priv.cpp", "ni_rsrc_api.cpp"];

        cc::Build::new()
            .files(cpp_source_files.iter().map(|name| source_path.join(name)))
            .warnings(false)
            .flag("-fPIC")
            .flag("-Wno-unused-command-line-argument")
            .flag("-DLIBXCODER_OBJS_BUILD")
            .compile("xcoder-cpp-sys");

        let bindings = bindgen::Builder::default()
            .generate_comments(false)
            .header("src/lib.hpp")
            .allowlist_function("ni_.+")
            .allowlist_type("ni_.+")
            .allowlist_var("NI_.+")
            .allowlist_var("GC620_.+")
            .clang_arg(format!("-I{}", source_path.to_str().unwrap()))
            .generate()
            .expect("unable to generate bindings");

        bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
    }
}
