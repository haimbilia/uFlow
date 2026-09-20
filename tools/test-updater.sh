#!/bin/sh
set -eu
testdir=$(mktemp -d /tmp/uflow-update-build.XXXXXX)
gcc -O2 -c /project/app/vendor/monocypher/monocypher.c -o "$testdir/mono.o"
gcc -O2 -c /project/app/vendor/monocypher/monocypher-ed25519.c -o "$testdir/ed.o"
g++ -std=c++20 -O2 -I/project/app/source /project/tests/update_test.cpp "$testdir/mono.o" "$testdir/ed.o" -o "$testdir/test"
"$testdir/test"
if [ -n "${UFLOW_TEST_UPDATE_RELEASE:-}" ]; then
    "$testdir/test" "$UFLOW_TEST_UPDATE_RELEASE/manifest.json" "$UFLOW_TEST_UPDATE_BINARY"
fi
