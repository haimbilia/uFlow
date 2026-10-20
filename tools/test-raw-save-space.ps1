$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$source = Get-Content -LiteralPath (Join-Path $project 'module/src/RPXLoading.cpp') -Raw
$start = $source.IndexOf('DECL_FUNCTION(FSStatus, Raw_FSGetFreeSpaceSize,')
$end = $source.IndexOf('DECL_FUNCTION(void, OSRestartGame,', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Production save-space hook boundaries not found' }
$hooks = $source.Substring($start, $end - $start)
foreach ($entry in @('FSGetFreeSpaceSize', 'FSGetFreeSpaceSizeAsync', 'FSFlushQuota', 'FSFlushQuotaAsync')) {
    if (!$source.Contains("REPLACE_FUNCTION_FOR_PROCESS(Raw_$entry, LIBRARY_COREINIT, $entry, FP_TARGET_PROCESS_GAME)")) {
        throw "Missing game-only hook registration: $entry"
    }
}
$fixture = Get-Content -LiteralPath (Join-Path $project 'tests/raw_save_space_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_HOOKS', $hooks) | docker run --rm -i --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/module/src -x c++ -o /tmp/raw-save-space - && /tmp/raw-save-space'
if ($LASTEXITCODE -ne 0) { throw 'Production save-space regression failed' }
