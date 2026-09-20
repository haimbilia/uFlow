$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$source = Get-Content -LiteralPath (Join-Path $project 'payload/source/ios_mcp/source/mcp_loadfile.c') -Raw
function Section([string]$start, [string]$end) {
    $a = $source.IndexOf($start)
    $b = $source.IndexOf($end, $a)
    if ($a -lt 0 -or $b -le $a) { throw "Missing production boundary: $start" }
    return $source.Substring($a, $b - $a)
}
$hook = Section 'int _MCP_LoadFile_patch(' 'void _MassEffect3LaunchDetected('
$cases = Section '            case UFLOW_USB_MAP_COMMAND:' '            case IPC_CUSTOM_START_MCP_THREAD:'
$types = Section 'typedef enum {' 'static const RPXFileReplacements gDefaultReplacements'
# Fixed IOSU entrypoints are mocked; all changed dispatch/lifecycle code is compiled verbatim.
$types = $types -replace '(?s)int \(\*const real_MCP_LoadFile\).*?0x05017248 \+ 1\);', ''
$fixture = Get-Content -LiteralPath (Join-Path $project 'tests/raw_rpl_test.c') -Raw
$ipc = Get-Content -LiteralPath (Join-Path $project 'payload/source/common/ipc_defs.h') -Raw
$requestType = $ipc.Substring($ipc.IndexOf('typedef enum {'), $ipc.IndexOf('#define MOCHA_API_VERSION') - $ipc.IndexOf('typedef enum {'))
$fixture = $fixture.Replace('// PRODUCTION_REQUEST_TYPE', $requestType)
$fixture.Replace('// PRODUCTION_TYPES', $types).Replace('// PRODUCTION_HOOK', $hook).Replace('// PRODUCTION_CASES', $cases) |
    docker run --rm -i --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'gcc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/payload/source/common -x c -o /tmp/raw-rpl - && /tmp/raw-rpl'
if ($LASTEXITCODE -ne 0) { throw 'Raw RPL production regression failed' }
