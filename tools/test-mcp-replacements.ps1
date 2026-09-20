$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$source = Get-Content -LiteralPath (Join-Path $project 'payload/source/ios_mcp/source/mcp_loadfile.c') -Raw
if ($source -match '\bsnprintf\s*\(') { throw 'MCP loader paths must not use the fixed firmware formatter' }
function Section([string]$start, [string]$end) {
    $a = $source.IndexOf($start)
    $b = $source.IndexOf($end, $a)
    if ($a -lt 0 -or $b -le $a) { throw "Missing production boundary: $start" }
    return $source.Substring($a, $b - $a)
}
$production = Section 'typedef enum {' 'int _MCP_LoadFile_patch('
# Only firmware entrypoints/environment memory are replaced. Path construction,
# lookup, SD/MLC replacement I/O routing and lifetime management run verbatim.
$production = $production -replace '(?s)int \(\*const real_MCP_LoadFile\).*?0x05017248 \+ 1\);', ''
$production = $production.Replace('((char *) 0x0511FF00)', '(environment)')
$hook = Section 'int _MCP_LoadFile_patch(' 'void _MassEffect3LaunchDetected('
$ipc = Get-Content -LiteralPath (Join-Path $project 'payload/source/common/ipc_defs.h') -Raw
$request = $ipc.Substring($ipc.IndexOf('typedef enum {'), $ipc.IndexOf('#define MOCHA_API_VERSION') - $ipc.IndexOf('typedef enum {'))
$fixture = Get-Content -LiteralPath (Join-Path $project 'tests/mcp_replacements_test.c') -Raw
$fixture.Replace('// PRODUCTION_REQUEST_TYPE', $request).Replace('// PRODUCTION_REPLACEMENTS', $production).Replace('// PRODUCTION_HOOK', $hook) |
    docker run --rm -i --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/payload/source/common -x c -o /tmp/mcp-replacements - && /tmp/mcp-replacements'
if ($LASTEXITCODE -ne 0) { throw 'MCP replacement production regression failed' }
