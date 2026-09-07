param(
    [string]$SourceRoot = (Join-Path $PSScriptRoot '..\..\..\Media'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\.media-build')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$source = (Resolve-Path -LiteralPath $SourceRoot).Path
$output = [System.IO.Path]::GetFullPath($OutputRoot)
$folders = 'covers', 'backgrounds', 'logos', 'previews', 'metadata'
foreach ($folder in $folders) {
    [System.IO.Directory]::CreateDirectory((Join-Path $output $folder)) | Out-Null
}

function Get-MediaKey([string]$name) {
    $value = [System.IO.Path]::GetFileNameWithoutExtension($name).ToLowerInvariant()
    $value = $value -replace '\s*\((usa|europe|japan|australia|korea|world)\)\s*$', ''
    $value = $value -replace '[^a-z0-9]+', '_'
    return $value.Trim('_')
}

function Get-DisplayName([string]$name) {
    $value = [System.IO.Path]::GetFileNameWithoutExtension($name)
    $value = $value -replace '\s*\((USA|Europe|Japan|Australia|Korea|World)\)\s*$', ''
    return ($value -replace '_', "'")
}

function Get-Region([string]$name) {
    if ($name -match '\((USA|Europe|Japan|Australia|Korea|World)\)') { return $Matches[1].ToUpperInvariant() }
    return 'UNKNOWN'
}

function Convert-Png([string]$inputPath, [string]$outputPath, [int]$maxWidth, [int]$maxHeight) {
    $sourceImage = [System.Drawing.Image]::FromFile($inputPath)
    try {
        $scale = [Math]::Min($maxWidth / $sourceImage.Width, $maxHeight / $sourceImage.Height)
        $scale = [Math]::Min(1.0, $scale)
        $width = [Math]::Max(1, [int][Math]::Round($sourceImage.Width * $scale))
        $height = [Math]::Max(1, [int][Math]::Round($sourceImage.Height * $scale))
        $bitmap = New-Object System.Drawing.Bitmap($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.DrawImage($sourceImage, 0, 0, $width, $height)
            } finally { $graphics.Dispose() }
            $bitmap.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
        } finally { $bitmap.Dispose() }
    } finally { $sourceImage.Dispose() }
}

$coverCount = 0
$logoCount = 0
$metadataCount = 0
$covers = Get-ChildItem -LiteralPath (Join-Path $source 'FINISHED') -File -Filter '*.png' |
    Sort-Object @{ Expression = { if ($_.Name -match '\(USA\)') { 0 } else { 1 } } }, Name
foreach ($file in $covers) {
    $key = Get-MediaKey $file.Name
    if (-not $key) { continue }
    $destination = Join-Path $output "covers\$key.png"
    if (-not (Test-Path -LiteralPath $destination)) {
        Convert-Png $file.FullName $destination 420 600
        $coverCount++
    }
    $metadata = Join-Path $output "metadata\$key.ini"
    if (-not (Test-Path -LiteralPath $metadata)) {
        $lines = @(
            "title=$(Get-DisplayName $file.Name)"
            "region=$(Get-Region $file.Name)"
            'publisher='
            'developer='
            'genre='
            'year='
            'players='
            'rating='
            'description='
        )
        [System.IO.File]::WriteAllLines($metadata, $lines, [System.Text.UTF8Encoding]::new($false))
        $metadataCount++
    }
}

$logoSources = @(
    (Join-Path $source 'logos Updated (better,fixed and so on)'),
    (Join-Path $source 'logos')
)
foreach ($logoSource in $logoSources) {
    if (-not (Test-Path -LiteralPath $logoSource)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $logoSource -File -Filter '*.png') {
        $key = Get-MediaKey $file.Name
        if (-not $key) { continue }
        $destination = Join-Path $output "logos\$key.png"
        if (-not (Test-Path -LiteralPath $destination)) {
            Convert-Png $file.FullName $destination 500 220
            $logoCount++
        }
    }
}

foreach ($kind in 'backgrounds', 'previews', 'metadata') {
    $optionalSource = Join-Path $source $kind
    if (-not (Test-Path -LiteralPath $optionalSource)) { continue }
    Copy-Item -LiteralPath (Join-Path $optionalSource '*') -Destination (Join-Path $output $kind) -Force -Recurse
}

Write-Host "Prepared $coverCount covers, $logoCount logos, and $metadataCount metadata records."
Write-Host "Output: $output"
