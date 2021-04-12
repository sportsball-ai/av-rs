fn main() {
    #[cfg(target_os = "macos")]
    {
        println!("cargo:rustc-link-lib=framework=Foundation");
        println!("cargo:rustc-link-lib=framework=AVFoundation");
        println!("cargo:rustc-link-lib=c++");

        cc::Build::new()
            .files(
                [
                    "src/lib.mm",
                    "src/av_capture_video_data_output.mm",
                    "src/av_capture_device.mm",
                    "src/av_media_type.mm",
                    "src/av_capture_session.mm",
                ]
                .iter(),
            )
            .flag("-fobjc-arc")
            .compile("av-foundation");
    }
}
