$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$main=Get-Content (Join-Path $project 'app/source/main.cpp') -Raw
if(!$main.Contains('const bool wiiVC=WiiVC::IsRaw(game);')){throw 'Wii VC HAI launch selection is missing'}
if(!$main.Contains('LoaderSupportsWiiVCHai()')){throw 'Wii VC must require the HAI-capable raw backend'}
$prepare=[regex]::Matches($main,'WiiVC::Prepare\(').Count
if($prepare -ne 1){throw "Wii VC conversion must have exactly one call site (found $prepare)"}
if(!$main.Contains('if (wiiVC && launchMethod==LaunchMethod::Injection) {')){throw 'Wii VC conversion must only run when Injection is chosen in Launch Options'}
if($main.IndexOf('if (wiiVC && launchMethod==LaunchMethod::Injection) {') -gt $main.IndexOf('WiiVC::Prepare(')){throw 'Wii VC conversion must be inside the explicit Injection branch'}
if($main.Contains('wii-vc-direct-selected')){throw 'Wii VC must not use the pre-HAI direct launch path'}
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" uflow-visual-preview:video sh -c 'set -eu; testdir=$(mktemp -d); g++ -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I/project/app/source /project/tests/wii_vc_cancel_test.cpp /project/app/source/wii_vc.cpp $(pkg-config --cflags --libs libavutil) -o "$testdir/check"; cd "$testdir"; ./check; sh /project/tools/test-wii-launch.sh'
if($LASTEXITCODE -ne 0){throw 'Wii VC cancellation checks failed'}
