$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$source=Get-Content -LiteralPath (Join-Path $project 'module/src/RPXLoading.cpp') -Raw
$start=$source.IndexOf('DECL_FUNCTION(FSStatus, Raw_FSReadFileAsync,')
$end=$source.IndexOf('// The raw-game trace reached',$start)
if($start -lt 0 -or $end -le $start){throw 'Read-hook boundaries missing'}
$fixture=Get-Content -LiteralPath (Join-Path $project 'tests/raw_performance_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_READ_HOOKS',$source.Substring($start,$end-$start)) | docker run --rm -i --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -I/project/module/src -x c++ -o /tmp/raw-performance - && /tmp/raw-performance'
if($LASTEXITCODE -ne 0){throw 'Performance hook regression failed'}
