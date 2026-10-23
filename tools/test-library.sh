#!/bin/sh
set -eu
# Mount project read-only at /project; all fixtures/binaries stay in fresh /tmp.
for test in ${UFLOW_LIBRARY_TESTS:-browser_path injection_dialog installed_storage installed_discovery library library_no_replace wii_usb_scan media rom_art builtin_icon_cache launcher_overrides media_groups case_sleeve cover_faces cover_transition settings vc_sort content_type_media}; do
    testdir=$(mktemp -d /tmp/uflow-scan-test.XXXXXX)
    source=$test
    extra=''
    if [ "$test" = installed_discovery ]; then extra='-DUFLOW_TEST_INSTALLED'; fi
    if [ "$test" = installed_storage ]; then extra='-DUFLOW_TEST_INSTALLED -DUFLOW_TEST_INSTALLED_MOUNTS'; fi
    if [ "$test" = library_no_replace ]; then
        source=library
        extra='/project/tests/no_replace_rename.cpp -Wl,--wrap=rename'
    fi
    if [ "$test" = settings ] || [ "$test" = library_sort ] || [ "$test" = vc_sort ]; then extra='/project/app/source/settings.cpp'; fi
    g++ -std=c++20 -O2 -I/project/tests/stubs -I/project/app/source \
        /project/tests/${source}_test.cpp $extra /project/app/source/library.cpp \
        /project/app/source/media.cpp /project/app/source/raw_launch.cpp -o "$testdir/check"
    (cd "$testdir" && ./check)
done
