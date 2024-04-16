use std::fs::File;

use h265_tile_mux::*;

type BoxError = Box<dyn std::error::Error + Sync + Send>;

fn main() -> Result<(), BoxError> {
    let matches = clap::App::new("h265-tile-mux")
        .about("combines tiles from multiple videos into a single video")
        .arg(
            clap::Arg::with_name("input")
                .long("input")
                .short("i")
                .help("the input files. these must be raw h265 files")
                .multiple(true)
                .required(true)
                .takes_value(true),
        )
        .arg(
            clap::Arg::with_name("selection")
                .long("selection")
                .short("s")
                .help("the input index (starting with 0) to use for each tile")
                .multiple(true)
                .required(true)
                .takes_value(true),
        )
        .arg(
            clap::Arg::with_name("output")
                .long("output")
                .short("o")
                .help("the output file")
                .required(true)
                .takes_value(true),
        )
        .version(env!("CARGO_PKG_VERSION"))
        .get_matches();

    let inputs = matches.values_of("input").unwrap().map(File::open).collect::<Result<Vec<_>, _>>()?;

    let selection = matches
        .values_of("selection")
        .unwrap()
        .map(|s| -> Result<usize, BoxError> {
            let i = s.parse()?;
            if i >= inputs.len() {
                Err("selection out of range".into())
            } else {
                Ok(i)
            }
        })
        .collect::<Result<Vec<_>, _>>()?;

    let output = File::create(matches.value_of("output").unwrap())?;

    mux(inputs.into_iter().map(h265::read_annex_b), &selection, output)?;

    Ok(())
}
