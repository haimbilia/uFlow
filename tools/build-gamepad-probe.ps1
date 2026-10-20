[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$forwarder = Join-Path $project 'wii/gamepad-probe/cafe'
python (Join-Path $project 'tests/test_gamepad_transport.py')
if ($LASTEXITCODE -ne 0) { throw 'Firmware inspection tests failed' }
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined /project/tests/gamepad_probe_test.cpp -o /tmp/probe-test && /tmp/probe-test'
if ($LASTEXITCODE -ne 0) { throw 'Packet decoder tests failed' }
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined /project/tests/gamepad_transport_test.cpp -o /tmp/transport-test && /tmp/transport-test'
if ($LASTEXITCODE -ne 0) { throw 'Bounded transport tests failed' }
docker run --rm --mount "type=bind,source=$project,target=/project,readonly" gcc:14-bookworm sh -c 'g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined /project/tests/gamepad_prepare_test.cpp -o /tmp/prepare-test && /tmp/prepare-test'
if ($LASTEXITCODE -ne 0) { throw 'Launch preparation tests failed' }
docker run --rm --mount "type=bind,source=$project,target=/project" devkitpro/devkitppc:20250527 make -C /project/wii/gamepad-probe
if ($LASTEXITCODE -ne 0) { throw 'vWii probe build failed' }
docker run --rm --mount "type=bind,source=$project,target=/project" uflow-builder:video make -C /project/wii/gamepad-probe/cafe
if ($LASTEXITCODE -ne 0) { throw 'Probe forwarder build failed' }
$output = Join-Path $project 'artifacts/gamepad-probe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Copy-Item -LiteralPath (Join-Path $forwarder 'uFlow-GamePad-Probe.wuhb') -Destination $output
Copy-Item -LiteralPath (Join-Path $project 'wii/gamepad-probe/boot.dol') -Destination $output
Get-FileHash -LiteralPath (Join-Path $output 'uFlow-GamePad-Probe.wuhb'),(Join-Path $output 'boot.dol')
