fn main() {
    #[cfg(target_os = "macos")]
    {
        #[cfg(target_arch = "x86_64")]
        println!("cargo:rustc-link-search={}/vendor/macos-x86_64/lib", env!("CARGO_MANIFEST_DIR"));
        #[cfg(target_arch = "aarch64")]
        println!("cargo:rustc-link-search={}/vendor/macos-aarch64/lib", env!("CARGO_MANIFEST_DIR"));

        println!("cargo:rustc-link-lib=c++");
    }

    #[cfg(target_os = "linux")]
    {
        println!("cargo:rustc-link-search={}/vendor/linux/lib", env!("CARGO_MANIFEST_DIR"));
        println!("cargo:rustc-link-lib=stdc++");
        println!("cargo:rustc-link-lib=crypto");
    }
}
