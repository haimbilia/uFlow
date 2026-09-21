$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$source = Get-Content -LiteralPath (Join-Path $project 'module/src/RPXLoading.cpp') -Raw
$start = $source.IndexOf('static RawExitTraceState sRawExitTrace;')
$end = $source.IndexOf('static std::atomic<uint32_t> sRawReadSamples', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Exit trace boundaries not found' }
$fixture = Get-Content -LiteralPath (Join-Path $project 'tests/raw_exit_trace_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_EXIT_TRACE', $source.Substring($start, $end - $start)) | docker run --rm -i --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/module/src -x c++ -o /tmp/exit-trace - && /tmp/exit-trace'
if ($LASTEXITCODE -ne 0) { throw 'Production exit trace regression failed' }
