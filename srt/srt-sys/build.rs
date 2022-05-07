fn main() {
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").expect("CARGO_CFG_TARGET_OS must be set");
    let target_arch = std::env::var("CARGO_CFG_TARGET_ARCH").expect("CARGO_CFG_TARGET_ARCH must be set");

    let mut build = cmake::Config::new("vendor/srt-1.4.4");
    build.define("ENABLE_APPS", "OFF").define("ENABLE_SHARED", "OFF");

    if target_os == "macos" {
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
}
