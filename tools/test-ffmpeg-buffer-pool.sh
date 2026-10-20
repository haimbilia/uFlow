#!/bin/sh
set -eu
# Pass the pinned FFmpeg tree from the native builder (including config.h).
source_root=${1:?provide patched FFmpeg source root}
testdir=$(mktemp -d /tmp/uflow-buffer-pool.XXXXXX)
cc -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
    -DFF_MEMORY_POISON=0x2a -I/project/tests/ffmpeg-stubs -I"$source_root" \
    "$source_root/libavutil/buffer.c" /project/tests/ffmpeg_buffer_pool_test.c \
    $(pkg-config --cflags --libs libavutil) -o "$testdir/check"
"$testdir/check"
