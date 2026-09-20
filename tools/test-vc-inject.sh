#!/bin/sh
set -eu
testdir=$(mktemp -d /tmp/uflow-vc-test.XXXXXX)
extra=''
if [ "${UFLOW_TEST_NO_REPLACE:-0}" = 1 ]; then
    extra='/project/tests/no_replace_rename.cpp -Wl,--wrap=rename'
fi
g++ -std=c++20 -O2 -I/project/app/source /project/tests/vc_inject_test.cpp $extra /project/app/source/raw_launch.cpp -lz -lavutil -o "$testdir/check"
cd "$testdir"
./check
if [ -d /vc-base ] && [ -f /test.nds ]; then
    mkdir real
    cd real
    mkdir -p fs:/vol/external01/bases/test
    "$testdir/check" /vc-base /test.nds
fi
