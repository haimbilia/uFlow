#!/bin/sh
set -eu
# Mount project read-only at /project; all fixtures/binaries stay in fresh /tmp.
for test in ${UFLOW_LIBRARY_TESTS:-injection_dialog library library_no_replace media rom_art launcher_overrides media_groups case_sleeve cover_faces cover_transition}; do
    testdir=$(mktemp -d /tmp/uflow-scan-test.XXXXXX)
    source=$test
    extra=''
    if [ "$test" = library_no_replace ]; then
        source=library
        extra='/project/tests/no_replace_rename.cpp -Wl,--wrap=rename'
    fi
    g++ -std=c++20 -O2 -I/project/tests/stubs -I/project/app/source \
        /project/tests/${source}_test.cpp $extra /project/app/source/library.cpp \
        /project/app/source/media.cpp /project/app/source/raw_launch.cpp -o "$testdir/check"
    (cd "$testdir" && ./check)
done
