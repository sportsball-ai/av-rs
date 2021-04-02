use std::fs::File;

mod lib;
use lib::*;

type BoxError = Box<dyn std::error::Error + Sync + Send>;

fn main() -> Result<(), BoxError> {
    let matches = clap::App::new("h265-tile-join")
        .about("joins multiple untiled streams into one tiled stream")
        .arg(
            clap::Arg::with_name("input")
                .long("input")
                .short("i")
                .help("the input files. these must be raw h265")
                .multiple(true)
                .required(true)
                .takes_value(true),
        )
        .arg(
            clap::Arg::with_name("output")
                .long("output")
                .short("o")
                .help("the output path")
                .required(true)
                .takes_value(true),
        )
        .version(env!("CARGO_PKG_VERSION"))
        .get_matches();

    let inputs = matches.values_of("input").unwrap().map(File::open).collect::<Result<Vec<_>, _>>()?;

    let output = File::create(matches.value_of("output").unwrap())?;

    join(inputs.into_iter().map(h265::read_annex_b), output)?;

    Ok(())
}
