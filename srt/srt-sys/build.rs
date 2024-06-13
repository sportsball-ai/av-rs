fn main() {
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").expect("CARGO_CFG_TARGET_OS must be set");
    let target_arch = std::env::var("CARGO_CFG_TARGET_ARCH").expect("CARGO_CFG_TARGET_ARCH must be set");

    let mut build = cmake::Config::new("vendor/srt-1.4.4");
    build.define("ENABLE_APPS", "OFF").define("ENABLE_SHARED", "OFF");

    if target_os == "macos" {
        build.define(
            "CMAKE_OSX_ARCHITECTURES",
            match target_arch.as_str() {
                "aarch64" => "arm64",
                arch => arch,
            },
        );
        build.define("USE_OPENSSL_PC", "OFF");
        build.define("OPENSSL_INCLUDE_DIR", format!("{}/vendor/openssl/include", env!("CARGO_MANIFEST_DIR")));
        build.define(
            "OPENSSL_CRYPTO_LIBRARY",
            format!("{}/vendor/openssl/lib/macos-{}/libcrypto.a", env!("CARGO_MANIFEST_DIR"), target_arch),
        );
        build.define("OPENSSL_SSL_LIBRARY", ""); // libssl isn't needed since we aren't building the apps
    }

    let build = build.build();

    println!("cargo:rustc-link-search={}/lib", build.display());
    println!("cargo:rustc-link-lib=static=srt");

    match target_os.as_str() {
        "macos" => {
            println!("cargo:rustc-link-lib=c++");
            println!(
                "cargo:rustc-link-search={}/vendor/openssl/lib/macos-{}",
                env!("CARGO_MANIFEST_DIR"),
                target_arch
            );
            println!("cargo:rustc-link-lib=static=crypto");
        }
        "linux" => {
            println!("cargo:rustc-link-lib=c");
            println!("cargo:rustc-link-lib=stdc++");
            println!("cargo:rustc-link-lib=crypto");
        }
        _ => {}
    }

    let bindings = bindgen::Builder::default()
        .header(format!("{}/include/srt/srt.h", build.display()))
        .allowlist_function("srt_.+")
        .allowlist_type("SRT_.+")
        .generate()
        .expect("unable to generate bindings");

    let out_path = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap());
    bindings.write_to_file(out_path.join("bindings.rs")).expect("unable to write bindings");
}
