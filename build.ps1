[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$app = Join-Path $PSScriptRoot 'app'
$dist = Join-Path $PSScriptRoot 'dist'
$stage = Join-Path $dist 'stage'
$zip = Join-Path $dist 'uFlow-v0.1.0-dev.zip'

docker build --pull=false --provenance=false -t uflow-builder:dev $app
if ($LASTEXITCODE -ne 0) { throw 'Builder image failed.' }

docker run --rm --mount "type=bind,source=$app,target=/project" uflow-builder:dev make clean all
if ($LASTEXITCODE -ne 0) { throw 'uFlow build failed.' }

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'wiiu\apps\uFlow') | Out-Null
Copy-Item -LiteralPath (Join-Path $app 'uFlow.wuhb') -Destination (Join-Path $stage 'wiiu\apps\uFlow\uFlow.wuhb')

if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Get-FileHash -Algorithm SHA256 (Join-Path $app 'uFlow.wuhb'), $zip | Format-Table -AutoSize

