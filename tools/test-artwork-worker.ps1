$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" uflow-visual-preview:video sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/app/source /project/tests/artwork_worker_test.cpp $(pkg-config --cflags --libs sdl2) -o /tmp/artwork-worker && /tmp/artwork-worker'
if ($LASTEXITCODE -ne 0) { throw 'Artwork worker regression failed' }
