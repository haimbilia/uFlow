[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
# Extends the video builder with the pinned SDL compressed-texture backend.
docker build -f (Join-Path $PSScriptRoot 'cover-cache.Dockerfile') -t uflow-builder:covers $project
if ($LASTEXITCODE -ne 0) { throw 'Cover texture builder failed' }
docker build -f (Join-Path $PSScriptRoot 'video-colour.Dockerfile') -t uflow-builder:video-colour $project
if ($LASTEXITCODE -ne 0) { throw 'Video colour builder failed' }
docker build -f (Join-Path $PSScriptRoot 'video-nv12.Dockerfile') -t uflow-builder:video-nv12 $project
if ($LASTEXITCODE -ne 0) { throw 'Native video planes builder failed' }
docker run --rm --mount "type=bind,source=$project,target=/project" uflow-builder:video-nv12 make -C /project/app -j4
if ($LASTEXITCODE -ne 0) { throw 'uFlow app build failed' }
