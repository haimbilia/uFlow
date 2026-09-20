#!/bin/sh
set -eu
test_root=$(mktemp -d /tmp/uflow-music-test.XXXXXX)
cd "$test_root"
for ext in wav mp3 ogg; do
    ffmpeg -v error -f lavfi -i 'sine=frequency=440:duration=0.2' "tone.$ext"
done
g++ -std=c++20 -O2 -I/project/app/source /project/tests/music_test.cpp /project/app/source/music.cpp /project/app/source/settings.cpp $(pkg-config --cflags --libs sdl2 SDL2_mixer) -o test
SDL_AUDIODRIVER=dummy ./test "$@"
