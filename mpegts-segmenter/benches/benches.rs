use criterion::{criterion_group, criterion_main, Criterion};
use mpeg2::ts;
use std::{fs::File, io::Read};

fn criterion_benchmark(c: &mut Criterion) {
    c.bench_function("analyzer", |b| {
        let mut f = File::open("src/testdata/h264-8k.ts").unwrap();
        let mut buf = Vec::new();
        f.read_to_end(&mut buf).unwrap();
        let packets = ts::decode_packets(&buf).unwrap();

        b.iter(|| {
            let mut analyzer = mpegts_segmenter::Analyzer::new();
            analyzer.handle_packets(&packets).unwrap();
            analyzer.flush().unwrap();
        })
    });
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
