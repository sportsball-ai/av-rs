#!/usr/bin/env bash

rm xcoder.log
cd ./build

sudo ./xcoder /dev/nvme0 ../test/1280x720p_Basketball.264 dec_output.yuv 1280 720 decode > xcoder.log 2>&1
#sudo ./xcoder /dev/nvme0 ../test/output.yuv enc_output.265 1280 720 encode > xcoder.log 2>&1
#sudo ./xcoder /dev/nvme0 ../test/1280x720p_Basketball.264 xcod_output.265 1280 720 transcode > xcoder.log 2>&1
