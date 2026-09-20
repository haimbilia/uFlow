$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
docker run --rm --mount "type=bind,source=$project/module,target=/project" --mount "type=bind,source=$project/app,target=/app,readonly" --mount "type=bind,source=$project/payload,target=/payload,readonly" wiiu-loose-title-module-builder:0.1.0 make -C /project -j4
if($LASTEXITCODE -ne 0){throw 'USB raw module build failed'}
docker run --rm --mount "type=bind,source=$project/payload,target=/project" wiiu-raw-launcher-payload-builder:0.1.0 make -C /project -j4
if($LASTEXITCODE -ne 0){throw 'USB raw payload build failed'}
