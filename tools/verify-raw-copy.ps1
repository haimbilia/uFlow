param(
    [Parameter(Mandatory=$true)][string]$Source,
    [Parameter(Mandatory=$true)][string]$Destination
)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\')
$targetRoot = (Resolve-Path -LiteralPath $Destination).Path.TrimEnd('\')
if ($sourceRoot -eq $targetRoot) { throw 'Source and destination must differ' }
$checked = 0
$checkedBytes = 0L
$lastReport = [DateTime]::UtcNow
foreach ($section in @('code', 'content', 'meta')) {
    $sectionRoot = Join-Path $sourceRoot $section
    if (-not (Test-Path -LiteralPath $sectionRoot -PathType Container)) {
        throw "Missing source section: $sectionRoot"
    }
    foreach ($file in Get-ChildItem -LiteralPath $sectionRoot -File -Recurse) {
        $relative = $file.FullName.Substring($sourceRoot.Length + 1)
        $targetFile = Join-Path $targetRoot $relative
        if (-not (Test-Path -LiteralPath $targetFile -PathType Leaf)) {
            throw "Missing destination file: $relative"
        }
        $targetInfo = Get-Item -LiteralPath $targetFile
        if ($file.Length -ne $targetInfo.Length) { throw "Size mismatch: $relative" }
        $sourceHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $targetHash = (Get-FileHash -LiteralPath $targetFile -Algorithm SHA256).Hash
        if ($sourceHash -ne $targetHash) { throw "SHA256 mismatch: $relative" }
        $checked++
        $checkedBytes += $file.Length
        if (([DateTime]::UtcNow - $lastReport).TotalSeconds -ge 15) {
            Write-Output "Verified $checked files; $([math]::Round($checkedBytes / 1MB, 2)) MiB"
            $lastReport = [DateTime]::UtcNow
        }
    }
}
Write-Output "PASS: $checked files, $checkedBytes bytes; all SHA256 hashes match source."
