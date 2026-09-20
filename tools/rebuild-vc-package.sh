#!/bin/sh
set -eu
# Explicit operator utility; never invoked by ordinary test scripts.
test "$#" -eq 2
test "$1" = --publish
test -d /sd/wiiu
taskdir=$(mktemp -d /tmp/uflow-package-rebuild.XXXXXX)
g++ -std=c++20 -O2 -I/project/app/source /project/tools/rebuild-vc-package.cpp /project/app/source/raw_launch.cpp -lz -lavutil -o "$taskdir/rebuild"
cd "$taskdir"
mkdir -p fs:/vol
ln -s /sd fs:/vol/external01
./rebuild "$@"
