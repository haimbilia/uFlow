$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$sourceRoot=Join-Path (Split-Path (Split-Path $project -Parent) -Parent) 'tooling/sources/ftpiiu_plugin'
python (Join-Path $PSScriptRoot 'prepare-ftpiiu-exit.py') $sourceRoot
if($LASTEXITCODE -ne 0){throw 'FTPiiU source patch verification failed'}
$source=Get-Content (Join-Path $sourceRoot 'source/wiiu/platform.cpp') -Raw
$fixture=Get-Content (Join-Path $project 'tests/ftpiiu_exit_test.cpp') -Raw
$start=$source.IndexOf('void stop_server ()')
$end=$source.IndexOf('static void gFTPServerRunningChanged', $start)
if($start -lt 0 -or $end -le $start){throw 'Stop server boundaries missing'}
$fixture=$fixture.Replace('// PRODUCTION_STOP_SERVER',$source.Substring($start,$end-$start))
$start=$source.IndexOf('ON_APPLICATION_START ()')
$end=$source.IndexOf('bool platform::init ()', $start)
if($start -lt 0 -or $end -le $start){throw 'Lifecycle boundaries missing'}
$fixture.Replace('// PRODUCTION_LIFECYCLE',$source.Substring($start,$end-$start)) | docker run --rm -i gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -x c++ -o /tmp/ftp-exit - && /tmp/ftp-exit'
if($LASTEXITCODE -ne 0){throw 'FTPiiU lifecycle regression failed'}
