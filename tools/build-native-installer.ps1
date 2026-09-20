[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$sources = Join-Path (Split-Path (Split-Path $project -Parent) -Parent) 'tooling\sources'
# Changed components must be built/tested first. Synchronize release artifacts,
# then independently verify them inside the packaging build before compiling.
& (Join-Path $PSScriptRoot 'build-wiiflow-forwarder.ps1')
python (Join-Path $PSScriptRoot 'sync-native-payload.py') --sync --sources-root $sources
if ($LASTEXITCODE -ne 0) { throw 'Native payload synchronization failed' }
docker run --rm --mount "type=bind,source=$project,target=/project" --mount "type=bind,source=$sources,target=/sources,readonly" -e UFLOW_SOURCES_ROOT=/sources uflow-builder:video make -C /project/native-installer -j4
if ($LASTEXITCODE -ne 0) { throw 'Native installer build failed' }
Get-FileHash -LiteralPath (Join-Path $project 'native-installer\uFlow-Setup.wuhb')
