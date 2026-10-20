$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" uflow-visual-preview:video sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/app/source /project/tests/heap_owner_probe_test.cpp -o /tmp/heap-owner-test && /tmp/heap-owner-test'
if ($LASTEXITCODE -ne 0) { throw 'Heap owner diagnostic regression failed' }
