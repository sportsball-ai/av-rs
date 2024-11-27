fn main() {
    #[cfg(target_os = "macos")]
    {
        println!("cargo:rustc-link-lib=framework=Foundation");

        cc::Build::new()
            .files(["src/nserror.mm", "src/nsstring.mm"].iter())
            .flag("-fobjc-arc")
            .compile("foundation");
    }
}
