#!/bin/sh
set -eu
testdir=$(mktemp -d /tmp/uflow-usb-test.XXXXXX)
gcc -std=c11 -O2 -Wall -Wextra -I/project/app/vendor/fatfs \
    /project/tests/usb_fatfs_test.c /project/app/vendor/fatfs/ff.c \
    /project/app/vendor/fatfs/ffunicode.c -o "$testdir/check"
"$testdir/check"
UFLOW_LIBRARY_TESTS=browser_path sh /project/tools/test-library.sh
