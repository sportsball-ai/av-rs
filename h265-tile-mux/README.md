# h265-tile-mux

This crate can be used to combine several H265 videos of the same resolution and tile arrangement into a single video where each tile is selected from one of the input videos. This can be used for example to take a high quality video and a low quality video and combine them into one video where only a specified region is high quality. This is done without re-encoding the video, so it is extremely fast.

At the moment the best encoder for creating tiled videos is kvazaar. FFmpeg can be built with support for it and tiling can be enabled via `-kvazaar-params 'tiles=16x9,mv-constraint=frametilemargin,set-qp-in-cu=1'`.
