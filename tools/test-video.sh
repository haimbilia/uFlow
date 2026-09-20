#!/bin/sh
set -eu
testdir=$(mktemp -d /tmp/uflow-video-test.XXXXXX)
g++ -std=c++20 -O2 ${UFLOW_VIDEO_TEST_CXXFLAGS:-} -DUFLOW_HOST_PREVIEW -I/project/app/source \
    /project/tests/video_test.cpp /project/app/source/video.cpp \
    $(pkg-config --cflags --libs sdl2 SDL2_mixer libavformat libavcodec libavutil libswscale libswresample) -o "$testdir/check"
ffmpeg -v error -f lavfi -i testsrc2=size=320x180:rate=30 -f lavfi -i sine=frequency=440:sample_rate=44100 \
    -t 0.9 -c:v libx264 -pix_fmt yuv420p -c:a aac -movflags +faststart "$testdir/sample with spaces.mp4"
ffmpeg -v error -f lavfi -i testsrc2=size=320x240:rate=25 -f lavfi -i sine=frequency=550:sample_rate=48000 \
    -t 0.9 -c:v mpeg4 -c:a pcm_s16le "$testdir/sample.avi"
ffmpeg -v error -f lavfi -i testsrc2=size=320x180:rate=30 -t 0.9 -c:v mjpeg -threads 1 "$testdir/silent.avi"
ffmpeg -v error -f lavfi -i testsrc2=size=320x180:rate=30 -frames:v 1 -c:v libx264 -pix_fmt yuv420p "$testdir/oneframe.mp4"
UFLOW_VIDEO_TINY_SAMPLE="$testdir/oneframe.mp4" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$testdir/check" "$testdir/sample with spaces.mp4" "$testdir/sample.avi" "$testdir/silent.avi" ${UFLOW_VIDEO_SAMPLE:+"$UFLOW_VIDEO_SAMPLE"}
