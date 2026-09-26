param([string]$Source = 'D:/Projects/WiiU/tooling/sources/ContentRedirectionModule')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
python "$PSScriptRoot/prepare-content-remove-fix.py" $Source
if ($LASTEXITCODE -ne 0) { throw 'ContentRedirection patch validation failed' }
$sourceText = Get-Content -LiteralPath "$Source/src/FSWrapper.cpp" -Raw
$start = $sourceText.IndexOf('FSError FSWrapper::FSRemoveWrapper(')
$end = $sourceText.IndexOf('FSError FSWrapper::FSRenameWrapper(', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Removal implementation not found' }
$fixture = Get-Content -LiteralPath "$project/tests/content_remove_test.cpp" -Raw
$fixture.Replace('// PRODUCTION_REMOVE', $sourceText.Substring($start, $end - $start)) | docker run --rm -i gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -x c++ -o /tmp/test-remove - && /tmp/test-remove'
if ($LASTEXITCODE -ne 0) { throw 'Removal regression failed' }
