[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$sources = Join-Path (Split-Path (Split-Path $project -Parent) -Parent) 'tooling\sources\Boot2vWii'
# Separate object directory prevents reuse of the default Wii Menu target.
docker run --rm --mount "type=bind,source=$sources,target=/project" uflow-builder:video make -C /project -B BUILD=build-uflow-wiiflow TARGET=uFlow-WiiFlow TID=0x0001000144574641 'APP_NAME=WiiFlow' 'APP_SHORTNAME=WiiFlow' 'APP_AUTHOR=Boot2vWii / WiiFlow Team' ICON=assets/wiiflow.png DISPLAY_ON=BOTH FORCERES=NONE
if ($LASTEXITCODE -ne 0) { throw 'WiiFlow forwarder build failed' }
Get-FileHash -LiteralPath (Join-Path $sources 'uFlow-WiiFlow.wuhb')
