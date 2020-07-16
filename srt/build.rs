fn main() {
    #[cfg(target_os = "macos")]
    {
        println!("cargo:rustc-link-search={}/vendor/macos/lib", env!("CARGO_MANIFEST_DIR"));
        println!("cargo:rustc-link-lib=c++");
        println!("cargo:rustc-link-lib=crypto");
    }

    #[cfg(target_os = "linux")]
    {
        println!("cargo:rustc-link-search={}/vendor/linux/lib", env!("CARGO_MANIFEST_DIR"));
        println!("cargo:rustc-link-lib=stdc++");
        println!("cargo:rustc-link-lib=crypto");
    }
}
