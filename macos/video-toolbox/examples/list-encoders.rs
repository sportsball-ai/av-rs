// The whole video-toolbox package is macOS-specific, but CI just builds the whole workspace,
// and there's currently no way to edit the workspace package list by platform.
// <https://github.com/rust-lang/cargo/issues/5220>
#[cfg(not(target_os = "macos"))]
fn main() -> std::process::ExitCode {
    eprintln!("Unsupported on this platform.");
    std::process::ExitCode::FAILURE
}

#[cfg(target_os = "macos")]
fn main() {
    let encoders = video_toolbox::EncoderList::copy().unwrap();
    println!("{} supported encoders:", encoders.len());
    for i in 0..encoders.len() {
        let e = encoders.get(i);
        let id = e.id();
        let name = e.name();
        println!("  * {id}: {name}");
        println!("    supported properties:");
        for (k, v) in e.supported_properties(1920, 1024).unwrap() {
            println!("      - {}\n{}", k, &textwrap::indent(&format!("{:?}", v), "        "));
        }
    }
}
