$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$source = Get-Content -LiteralPath (Join-Path $project 'module/src/RPXLoading.cpp') -Raw
$start = $source.IndexOf('DECL_FUNCTION(FSStatus, Raw_FSOpenFile,')
$end = $source.IndexOf('static void PrepareCurrentTitleRelaunch()', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Production hook boundaries not found' }
$hooks = $source.Substring($start, $end - $start)
foreach ($entry in @('FSOpenFile', 'FSOpenFileAsync', 'FSOpenFileEx', 'FSOpenFileExAsync')) {
    if (!$source.Contains("REPLACE_FUNCTION(Raw_$entry, LIBRARY_COREINIT, $entry)")) {
        throw "Missing hook registration: $entry"
    }
}
$fixture = Get-Content -LiteralPath (Join-Path $project 'tests/raw_file_open_hooks_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_HOOKS', $hooks) | docker run --rm -i --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -I/project/module/src -x c++ -o /tmp/raw-file-opens - && /tmp/raw-file-opens'
if ($LASTEXITCODE -ne 0) { throw 'Production file-open regression failed' }
