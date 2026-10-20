#!/bin/sh
set -eu
dir=$(mktemp -d /tmp/uflow-region.XXXXXX)
g++ -std=c++20 -O2 -DUFLOW_TEST_LANGUAGE -I/project/tests/stubs -I/project/app/source \
 /project/tests/region_compatibility_test.cpp /project/app/source/library.cpp \
 /project/app/source/media.cpp /project/app/source/raw_launch.cpp -o "$dir/check"
(cd "$dir" && ./check)
