# h265-tile-join

This program will combine multiple untiled videos into a single video via tiling.

```
cargo run --features bin -- -i ./test/tiles/*.h265 -o ./test/out.h265
```

Constraints:

* All videos must be encoded using the same settings and have synchronized keyframes.
* All tiles must have resolutions divisible by their CTB size except for the right/bottom-most tiles.
