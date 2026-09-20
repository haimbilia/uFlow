param(
    [string]$SourceRoot = (Join-Path $PSScriptRoot '..\..\..\Media\_Source Artwork'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\.media-build\fullcovers'),
    [string]$Title = '*'
)
# Export original back|spine|front sleeves without touching the existing front art.
# No network downloads; sources and existing exports are never deleted.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$source = (Resolve-Path -LiteralPath $SourceRoot).Path
$output = [IO.Path]::GetFullPath($OutputRoot)
[IO.Directory]::CreateDirectory($output) | Out-Null
$prepared = 0
foreach ($directory in Get-ChildItem -LiteralPath $source -Directory | Where-Object Name -Like $Title) {
    if ($directory.Name -match '^[_!]') { continue } # reference/sort bins are not titles
    $key = ($directory.Name.ToLowerInvariant() -replace '\s*\((usa|europe|japan|australia|korea|world)\)\s*$', '' -replace '[^a-z0-9]+','_').Trim('_')
    $destination = Join-Path $output ($key + '.png')
    if (Test-Path -LiteralPath $destination) { continue }
    # Full sleeves are landscape ~284:190. Reject front covers, discs and 16:9 screenshots.
    $candidates = foreach ($file in Get-ChildItem -LiteralPath $directory.FullName -File) {
        if ($file.Extension -notin '.png','.jpg','.jpeg') { continue }
        $img = [Drawing.Image]::FromFile($file.FullName)
        try {
            $ratio = $img.Width / $img.Height
            if ($ratio -ge 1.44 -and $ratio -le 1.59 -and $img.Height -ge 500) {
                [PSCustomObject]@{ File=$file; Png=($file.Extension -eq '.png'); Area=($img.Width*$img.Height) }
            }
        } finally { $img.Dispose() }
    }
    $chosen = $candidates | Sort-Object Png,Area -Descending | Select-Object -First 1
    if (-not $chosen) { continue }
    $img = [Drawing.Image]::FromFile($chosen.File.FullName)
    try {
        $width = [Math]::Min(1024,$img.Width)
        $height = [int][Math]::Round($width*$img.Height/$img.Width)
        $bitmap = [Drawing.Bitmap]::new($width,$height,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.CompositingMode = [Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.DrawImage($img,0,0,$width,$height)
            } finally { $graphics.Dispose() }
            $bitmap.Save($destination,[Drawing.Imaging.ImageFormat]::Png)
        } finally { $bitmap.Dispose() }
    } finally { $img.Dispose() }
    $prepared++
    Write-Host "$key <- $($chosen.File.Name)"
}
Write-Host "Prepared $prepared full sleeves in $output"
