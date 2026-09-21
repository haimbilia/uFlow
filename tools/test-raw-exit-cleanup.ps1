$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$source=Get-Content (Join-Path $project 'module/src/main.cpp') -Raw
$start=$source.IndexOf('static std::atomic<bool> sApplicationCleanupDone')
$end=$source.IndexOf('WUMS_ALL_APPLICATION_REQUESTS_EXIT_DONE()', $start)
if($start -lt 0 -or $end -le $start){throw 'Shutdown boundaries missing'}
$fixture=Get-Content (Join-Path $project 'tests/raw_exit_cleanup_test.cpp') -Raw
$fixture=$fixture.Replace('// PRODUCTION_SHUTDOWN_CALLBACKS',$source.Substring($start,$end-$start))
$source=Get-Content (Join-Path $project 'module/src/RPXLoading.cpp') -Raw
$start=$source.IndexOf('    OSReport("uFlow raw profile:')
$end=$source.IndexOf('    bool success =', $start)
if($start -lt 0 -or $end -le $start){throw 'Profile log boundaries missing'}
$fixture.Replace('// PRODUCTION_PROFILE_LOG',$source.Substring($start,$end-$start)) | docker run --rm -i gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -x c++ -o /tmp/exit-cleanup - && /tmp/exit-cleanup'
if($LASTEXITCODE -ne 0){throw 'Shutdown cleanup regression failed'}
