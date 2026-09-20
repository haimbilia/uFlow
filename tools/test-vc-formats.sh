#!/bin/sh
set -eu
testdir=$(mktemp -d /tmp/uflow-formats.XXXXXX)

g++ -std=c++20 -O2 -Wall -Wextra -Werror -I/project/app/source /project/tests/n64_profile_test.cpp -o "$testdir/profiles"
"$testdir/profiles"
g++ -std=c++20 -O2 -I/project/app/source /project/tests/vc_formats_test.cpp /project/app/source/raw_launch.cpp -lz -lavutil -o "$testdir/check"
cd "$testdir"
for system in ${UFLOW_VC_SYSTEMS:-nes snes gba n64 pce msx}; do
    mkdir "$system"
    (cd "$system" && "$testdir/check" "/bases/$system" "$system")
done
