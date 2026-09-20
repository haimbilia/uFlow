#!/bin/sh
set -eu
# /project, /roms and /base are read-only mounts. All generated files are /tmp.
testdir=$(mktemp -d /tmp/uflow-nes.XXXXXX)
g++ -std=c++20 -O2 -I/project/app/source /project/tests/nes_formats_test.cpp -lz -lavutil -o "$testdir/formats"
g++ -std=c++20 -O2 -I/project/app/source /project/tests/nes_injection_test.cpp /project/app/source/raw_launch.cpp -lz -lavutil -o "$testdir/injection"
cd "$testdir"
./formats /roms /base
./injection
