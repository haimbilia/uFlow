$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" uflow-visual-preview:video sh -c 'set -eu; testdir=$(mktemp -d); g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/app/source /project/tests/wii_vc_cancel_test.cpp /project/app/source/wii_vc.cpp $(pkg-config --cflags --libs libavutil) -o "$testdir/check"; cd "$testdir"; ./check; sh /project/tools/test-wii-launch.sh'
if($LASTEXITCODE -ne 0){throw 'Wii VC cancellation checks failed'}
