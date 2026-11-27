$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$source = Get-Content -LiteralPath (Join-Path $project 'module/src/RPXLoading.cpp') -Raw
$start = $source.IndexOf('struct RawAOCTitle {')
$end = $source.IndexOf('function_replacement_data_t rpx_utils_function_replacements[]', $start)
$toggle = $source.IndexOf('static RPXLoaderStatus SetRawLayersActive(bool active)')
$toggleEnd = $source.IndexOf('RPXLoaderStatus RL_DisableContentRedirection()', $toggle)
if ($start -lt 0 -or $end -le $start -or $toggle -lt 0 -or $toggleEnd -le $toggle) { throw 'Production boundaries missing' }
$fixture = Get-Content -LiteralPath (Join-Path $project 'tests/raw_addon_hooks_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_AOC_HOOKS', $source.Substring($start, $end-$start)).Replace('// PRODUCTION_LAYER_TOGGLE', $source.Substring($toggle, $toggleEnd-$toggle)) | docker run --rm -i gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -x c++ -o /tmp/hooks - && /tmp/hooks'
if ($LASTEXITCODE -ne 0) { throw 'Production add-on hook test failed' }
