$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
function Section([string]$Text, [string]$Start, [string]$End) {
    $a=$Text.IndexOf($Start);$b=$Text.IndexOf($End,$a)
    if($a -lt 0 -or $b -le $a){throw "Missing production boundary: $Start"}
    return $Text.Substring($a,$b-$a)
}
$loader=Get-Content (Join-Path $project 'module/src/RetroArchLaunch.cpp') -Raw
$fixture=Get-Content (Join-Path $project 'tests/retroarch_loader_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_PROVIDER',(Section $loader 'namespace {' '// Everything this transaction')) |
    docker run --rm -i --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'set -eu; gcc -O1 -g -fsanitize=address,undefined -c /project/module/src/utils/ini.c -o /tmp/ini.o; g++ -std=c++20 -O1 -g -fsanitize=address,undefined -I/project/module/src -x c++ - -x none /tmp/ini.o -o /tmp/check; d=$(mktemp -d); cd "$d"; /tmp/check'
if($LASTEXITCODE -ne 0){throw 'RetroArch provider regression failed'}
$usb=Get-Content (Join-Path $project 'app/source/usb_storage.cpp') -Raw
$fixture=Get-Content (Join-Path $project 'tests/hai_source_test.cpp') -Raw
$fixture.Replace('// PRODUCTION_HAI_SOURCE',(Section $usb 'bool USBStorage::BuildHaiCompanion(' 'unsigned USBStorage::RawDevice()')) |
    docker run --rm -i --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++20 -O1 -g -fsanitize=address,undefined -I/project/app/source -x c++ - -o /tmp/check && /tmp/check'
if($LASTEXITCODE -ne 0){throw 'HAI source regression failed'}
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'set -eu; for test in retroarch_args retroarch_registry text_utf8 retroarch_modules launcher_module; do d=$(mktemp -d); g++ -std=c++20 -O1 -g -fsanitize=address,undefined -I/project/app/source -I/project/native-installer/source /project/tests/${test}_test.cpp -o "$d/check"; case "$test" in retroarch_modules) arg=/project/launcher-modules;; launcher_module) arg=/project/launcher-modules/noods.ufmod;; *) arg=;; esac; (cd "$d"; ./check $arg); done'
if($LASTEXITCODE -ne 0){throw 'Launcher/text review regressions failed'}
