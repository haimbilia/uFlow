# Explicit F: -> E: migration for the newly inspected 128 GB SD; never formats or deletes.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$logRoot = Join-Path $projectRoot 'dist'
$copyLog = Join-Path $logRoot 'sd128-copy.log'
$report = Join-Path $logRoot 'sd128-verification.log'
$sourceVolume = Get-Volume -DriveLetter F
$targetVolume = Get-Volume -DriveLetter E
if ($sourceVolume.Size -ne 7927234560 -or $targetVolume.Size -ne 127967133696 -or
    $targetVolume.FileSystem -ne 'FAT32') { throw 'Mounted cards differ from inspected F:/E: volumes.' }
if ((Get-Partition -DriveLetter E).DiskNumber -eq (Get-Partition -DriveLetter D).DiskNumber) {
    throw 'Destination is on protected D: disk.'
}
$jobs = @([pscustomobject]@{Source='F:\'; Target='E:\'})
foreach ($game in @('LEGO Batman 2 [Game] [0005000010135e00]', "Yoshi's Woolly World (USA) [AYCE01]")) {
    foreach ($section in @('code','content','meta')) {
        $jobs += [pscustomobject]@{
            Source = "D:\WiiU\$game\$section"
            Target = "E:\wiiu\raw-games\$game\$section"
        }
    }
}
$manifest = @()
foreach ($job in $jobs) {
    $root = (Resolve-Path -LiteralPath $job.Source).Path.TrimEnd('\')
    foreach ($file in Get-ChildItem -LiteralPath $job.Source -Recurse -File -Force) {
        $relative = $file.FullName.Substring($root.Length + 1)
        if ($job.Source -eq 'F:\' -and ($relative -like 'System Volume Information\*' -or $relative -like '$RECYCLE.BIN\*')) { continue }
        if ($file.Length -ge 4294967296) { throw "File exceeds FAT32 limit: $($file.FullName)" }
        $manifest += [pscustomobject]@{Source=$file.FullName; Target=(Join-Path $job.Target $relative); Length=$file.Length}
    }
}
$totalBytes = ($manifest | Measure-Object Length -Sum).Sum
if ($targetVolume.SizeRemaining -lt $totalBytes) { throw 'Insufficient destination free space.' }
"Preflight: $($manifest.Count) files, $totalBytes bytes" | Tee-Object -FilePath $report

# First retry the exact file that failed in Explorer before starting the batch.
$probeSource = 'F:\wiiu\raw-games\Shovel Knight\content\npcs_armorer.pak'
$probeTarget = 'E:\wiiu\raw-games\Shovel Knight\content\npcs_armorer.pak'
New-Item -ItemType Directory -Path (Split-Path $probeTarget -Parent) -Force | Out-Null
Copy-Item -LiteralPath $probeSource -Destination $probeTarget -Force
Write-VolumeCache -DriveLetter E
if ((Get-FileHash -LiteralPath $probeSource).Hash -ne (Get-FileHash -LiteralPath $probeTarget).Hash) {
    throw 'Small-file copy verification failed; stopped before batch.'
}
'Small-file copy/readback PASS' | Tee-Object -FilePath $report -Append
foreach ($job in $jobs) {
    "COPY: $($job.Source) -> $($job.Target)" | Tee-Object -FilePath $report -Append
    $options = @('/E','/COPY:DAT','/DCOPY:T','/R:0','/W:0','/XJ','/FFT','/IS','/IT','/NFL','/NDL','/NP',"/LOG+:$copyLog")
    if ($job.Source -eq 'F:\') { $options += @('/XD','F:\System Volume Information','F:\$RECYCLE.BIN') }
    & robocopy $job.Source $job.Target @options | Out-Host
    if ($LASTEXITCODE -ge 8) { throw "Copy failed with code $LASTEXITCODE; see $copyLog" }
}
Write-VolumeCache -DriveLetter E
'COPY COMPLETE; SHA256 verification starting' | Tee-Object -FilePath $report -Append
$checked = 0
$bytes = 0L
$lastReport = [DateTime]::UtcNow
foreach ($entry in $manifest) {
    $targetFile = Get-Item -LiteralPath $entry.Target
    if ($targetFile.Length -ne $entry.Length) { throw "Size mismatch: $($entry.Target)" }
    $sourceHash = (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash
    $targetHash = (Get-FileHash -LiteralPath $entry.Target -Algorithm SHA256).Hash
    if ($sourceHash -ne $targetHash) { throw "Hash mismatch: $($entry.Target)" }
    $checked++
    $bytes += $entry.Length
    if (([DateTime]::UtcNow - $lastReport).TotalSeconds -ge 20) {
        "VERIFIED: $checked/$($manifest.Count) files; $bytes/$totalBytes bytes" | Tee-Object -FilePath $report -Append
        $lastReport = [DateTime]::UtcNow
    }
}
Write-VolumeCache -DriveLetter E
"PASS: $checked files / $bytes bytes; all SHA256 hashes match sources. Windows volume bookkeeping excluded; no files deleted." | Tee-Object -FilePath $report -Append
