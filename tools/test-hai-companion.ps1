$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
python (Join-Path $project 'tests/test_hai_inspection.py')
if ($LASTEXITCODE -ne 0) { throw 'HAI inspection regression failed' }
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined /project/tests/hai_companion_test.cpp -o /tmp/hai-test && /tmp/hai-test'
if ($LASTEXITCODE -ne 0) { throw 'HAI companion serialization regression failed' }
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined /project/tests/raw_hai_arm_test.c -o /tmp/hai-arm && /tmp/hai-arm'
if ($LASTEXITCODE -ne 0) { throw 'HAI MCP arm validation regression failed' }
# A locally dumped title fw.img (ignored, never committed) adds a real r569 check.
$fw = if (Test-Path (Join-Path $project 'artifacts/hai-inspect/fw.img')) { '/project/artifacts/hai-inspect/fw.img' } else { '' }
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c "g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined /project/tests/hai_gamepad_test.cpp -o /tmp/hai-gamepad && /tmp/hai-gamepad $fw"
if ($LASTEXITCODE -ne 0) { throw 'HAI GamePad mapping regression failed' }
