$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$source=Get-Content -LiteralPath (Join-Path $project 'module/src/RPXLoading.cpp') -Raw
$start=$source.IndexOf('// BEGIN RAW STALL HOOKS')
$end=$source.IndexOf('// END RAW STALL HOOKS',$start)
if($start -lt 0 -or $end -le $start){throw 'Stall hook boundaries missing'}
foreach($entry in @('FSGetStat','FSCloseFile','FSSetPosFile')){
 if(!$source.Contains("REPLACE_FUNCTION_FOR_PROCESS(Raw_$entry, LIBRARY_COREINIT, $entry, FP_TARGET_PROCESS_GAME)")){throw "Missing scoped hook $entry"}
}
$fixture=Get-Content -LiteralPath (Join-Path $project 'tests/raw_stall_hooks_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_STALL_HOOKS',$source.Substring($start,$end-$start)) | docker run --rm -i gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -x c++ -o /tmp/stalls - && /tmp/stalls'
if($LASTEXITCODE -ne 0){throw 'Stall hook regression failed'}
