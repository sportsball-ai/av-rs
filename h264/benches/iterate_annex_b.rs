//! Benchmarks iteration over Annex B NALs.
//!
//! Expects a copy of [Big Buck Bunny](https://peach.blender.org/download/):
//! ```text
//! $ curl -OL https://download.blender.org/peach/bigbuckbunny_movies/big_buck_bunny_1080p_h264.mov
//! $ ffmpeg -i big_buck_bunny_1080p_h264.mov -c copy big_buck_bunny_1080p.h264
//! ```

use criterion::{criterion_group, criterion_main, Criterion};

fn criterion_benchmark(c: &mut Criterion) {
    let buf = std::fs::read("benches/testdata/big_buck_bunny_1080p.h264").unwrap();
    let mut g = c.benchmark_group("iterate_annex_b");
    g.throughput(criterion::Throughput::Bytes(buf.len() as u64));
    g.bench_function("iterate_annex_b", |b| {
        b.iter(|| {
            assert_eq!(h264::iterate_annex_b(&buf).count(), 130030);
        });
    });
    g.warm_up_time(std::time::Duration::from_secs(1));
    g.sampling_mode(criterion::SamplingMode::Flat);
    g.sample_size(10);
    g.finish();
}

criterion_group!(benches, criterion_benchmark);
criterion_main!(benches);
