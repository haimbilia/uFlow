#!/bin/sh
set -eu
# Run in uflow-visual-preview:video with the project mounted read-only.
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -I/project/app/source /project/tests/cover_texture_cache_test.cpp $(pkg-config --cflags --libs sdl2) -lz -o /tmp/uflow-cover-cache-test
/tmp/uflow-cover-cache-test
