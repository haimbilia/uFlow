$ErrorActionPreference = 'Stop'
$base = 'D:\WiiU\New Super Mario Bros [Game] [0005000010195a00]'
$remote = 'ftp://192.168.1.116/fs/vol/external01/wiiu/vc-bases/ds/nsmb-usa/'
$verify = Join-Path ([System.IO.Path]::GetTempPath()) ('uflow-base-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $verify | Out-Null
$files = foreach ($part in @('code','content','meta')) { Get-ChildItem -LiteralPath (Join-Path $base $part) -File -Recurse }
$count = 0
foreach ($file in $files) {
    $relative = $file.FullName.Substring($base.Length+1).Replace('\','/')
    $encoded = (($relative.Split('/') | ForEach-Object {[uri]::EscapeDataString($_)}) -join '/')
    $url = $remote + $encoded
    & curl.exe -q -sS --globoff --connect-timeout 5 --max-time 180 --user anonymous: --ftp-create-dirs -T $file.FullName $url
    if ($LASTEXITCODE) { throw "Upload failed: $relative" }
    $readback = Join-Path $verify 'readback.bin'
    & curl.exe -q -sS --globoff --connect-timeout 5 --max-time 180 --user anonymous: --output $readback $url
    if ($LASTEXITCODE) { throw "Readback failed: $relative" }
    if ((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $readback).Hash) { throw "Hash mismatch: $relative" }
    $count++
    if ($count % 20 -eq 0) { Write-Output "Verified $count / $($files.Count) base files" }
}
Write-Output "PASS: all $count VC base files uploaded and SHA-256 verified."
