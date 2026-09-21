$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$source = Get-Content -LiteralPath (Join-Path $project 'module/src/RPXLoading.cpp') -Raw
$start = $source.IndexOf('static UFlowReturn::MenuGate sCheckUFlowReturn;')
$end = $source.IndexOf('// Diagnostic boundaries.', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Return hook boundaries not found' }
if (!$source.Contains('REPLACE_FUNCTION_FOR_PROCESS(UFlow_ProcUIProcessMessages, LIBRARY_PROC_UI, ProcUIProcessMessages, FP_TARGET_PROCESS_WII_U_MENU)')) { throw 'Return hook is not restricted to Wii U Menu' }
$fixture = Get-Content -LiteralPath (Join-Path $project 'tests/return_hook_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_RETURN_HOOK', $source.Substring($start, $end - $start)) | docker run --rm -i --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -I/project/app/source -x c++ -o /tmp/return-hook - && /tmp/return-hook'
if ($LASTEXITCODE -ne 0) { throw 'Production return hook regression failed' }
