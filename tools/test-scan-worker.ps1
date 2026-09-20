$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" uflow-visual-preview:video sh -c 'set -eu; testdir=$(mktemp -d); g++ -std=c++20 -O1 -g -fsanitize=address,undefined -I/project/tests/stubs -I/project/app/source /project/tests/scan_worker_test.cpp /project/app/source/library.cpp /project/app/source/media.cpp /project/app/source/raw_launch.cpp $(pkg-config --cflags --libs sdl2) -o "$testdir/check"; cd "$testdir"; ./check'
if ($LASTEXITCODE -ne 0) { throw 'Scan worker regression failed' }
