$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$sourceRoot=Join-Path (Split-Path (Split-Path $project -Parent) -Parent) 'tooling/sources/ftpiiu_plugin'
python (Join-Path $PSScriptRoot 'prepare-ftpiiu-poll.py') $sourceRoot
if($LASTEXITCODE -ne 0){throw 'FTPiiU polling patch verification failed'}
$source=Get-Content (Join-Path $sourceRoot 'source/socket.cpp') -Raw
$start=$source.IndexOf('int Socket::poll (')
$end=$source.IndexOf('#ifdef __NDS__', $start)
if($start -lt 0 -or $end -le $start){throw 'Polling boundaries missing'}
$fixture=Get-Content (Join-Path $project 'tests/ftpiiu_poll_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_POLL',$source.Substring($start,$end-$start)) | docker run --rm -i gcc:14-bookworm sh -c 'g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -x c++ -o /tmp/ftp-poll - && /tmp/ftp-poll'
if($LASTEXITCODE -ne 0){throw 'FTPiiU polling regression failed'}
