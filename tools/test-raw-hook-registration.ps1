$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$source=Get-Content -LiteralPath (Join-Path $project 'module/src/main.cpp') -Raw
$start=$source.IndexOf('static void RegisterFunctionPatches() {')
$end=$source.IndexOf('WUMS_INITIALIZE()', $start)
if($start -lt 0 -or $end -le $start){throw 'Production registration not found'}
$fixture=Get-Content -LiteralPath (Join-Path $project 'tests/raw_hook_registration_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_REGISTRATION',$source.Substring($start,$end-$start)) | docker run --rm -i gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -x c++ -o /tmp/registration - && /tmp/registration'
if($LASTEXITCODE -ne 0){throw 'Hook registration regression failed'}
