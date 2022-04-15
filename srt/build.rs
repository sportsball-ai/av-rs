use std::env::var;

fn main() {
    match var("CARGO_CFG_TARGET_OS").expect("CARGO_CFG_TARGET_OS must be set").as_str() {
        "macos" => {
            match var("CARGO_CFG_TARGET_ARCH").expect("CARGO_CFG_TARGET_ARCH must be set").as_str() {
                "x86_64" => println!("cargo:rustc-link-search={}/vendor/macos-x86_64/lib", env!("CARGO_MANIFEST_DIR")),
                "aarch64" => println!("cargo:rustc-link-search={}/vendor/macos-aarch64/lib", env!("CARGO_MANIFEST_DIR")),
                _ => {}
            }
            println!("cargo:rustc-link-lib=c++");
        }
        "linux" => {
            println!("cargo:rustc-link-search={}/vendor/linux/lib", env!("CARGO_MANIFEST_DIR"));
            println!("cargo:rustc-link-lib=c");
            println!("cargo:rustc-link-lib=stdc++");
            println!("cargo:rustc-link-lib=crypto");
        }
        _ => {}
    }
}
