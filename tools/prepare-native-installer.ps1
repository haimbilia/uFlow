[CmdletBinding()]
param([string]$PackageRoot, [switch]$IncludeLocalBases, [string]$BaseRoot='D:\WiiU')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=Split-Path $PSScriptRoot -Parent
if (!$PackageRoot) { $PackageRoot=Join-Path $project 'dist\uFlow-Setup' }
. (Join-Path $project 'installer\Installer.Core.ps1')
$native=Join-Path $project 'native-installer'
$content=Join-Path $native 'content'
if (Test-Path -LiteralPath $content) { throw 'Native content already exists; preserve it and use a separate preparation output before replacing a release.' }
$null=New-Item -ItemType Directory -Path $content
$manifest=Get-Content -LiteralPath (Join-Path $PackageRoot 'manifest.json') -Raw | ConvertFrom-Json
$defs=New-Object 'Collections.Generic.List[string]'
$index=0;$ids=@{}
foreach ($component in Get-UFlowComponents) { $ids[$component.Id]=$index; $index++ }
function Literal([string]$Text) { ConvertTo-Json -InputObject $Text -Compress }
function Add-Native([string]$Source,[string]$Path,[string]$Hash,[long]$Size,[uint32]$Mask,[string]$Mode) {
    if ((Get-UFlowHash $Source) -ine $Hash -or (Get-Item -LiteralPath $Source).Length -ne $Size) { throw "Input changed: $Source" }
    $dest=Resolve-UFlowChild $content ('payload/'+$Path)
    $null=New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($dest)) -Force
    [IO.File]::Copy($Source,$dest,$false)
    if ((Get-UFlowHash $dest) -ine $Hash) { throw "Payload copy failed: $Path" }
    $defs.Add(('    {'+(Literal $Path)+','+(Literal $Hash.ToLowerInvariant())+','+$Size+'ULL,'+$Mask+"u,'"+$Mode+"'},"))
}
foreach ($file in $manifest.Files) {
    if ($file.Path -match '^wiiu/apps/uFlow/(libraries|content_sources|media_sources|uflow)\.cfg$') { continue }
    [uint32]$mask=0
    if (Test-UFlowSelectedPath $file.Path @{}) { $mask=2147483648 }
    else { foreach ($id in $ids.Keys) { if (Test-UFlowSelectedPath $file.Path @{$id=$true}) { $mask=$mask -bor (1 -shl $ids[$id]) } } }
    $mode=if ($file.Mode -eq 'keep') { 'k' } else { 'r' }
    Add-Native (Resolve-UFlowChild $PackageRoot ('payload/'+$file.Path)) $file.Path $file.Sha256 $file.Bytes $mask $mode
}
if (!$IncludeLocalBases) { throw 'This personal one-file build requires -IncludeLocalBases. A public base-picker build is not yet implemented.' }
foreach ($system in $manifest.Systems) {
    $matches=@(Get-ChildItem -LiteralPath $BaseRoot -Directory | Where-Object { $_.Name.EndsWith('['+$system.TitleId+']',[StringComparison]::OrdinalIgnoreCase) })
    if ($matches.Count -ne 1) { throw "Missing/ambiguous base: $($system.Id)" }
    $base=$matches[0].FullName
    $xml=New-Object Xml.XmlDocument;$xml.XmlResolver=$null;$xml.Load((Join-Path $base 'meta\meta.xml'))
    if ($xml.SelectSingleNode('/menu/title_id').InnerText.Trim() -ine $system.TitleId) { throw 'Wrong base metadata' }
    Write-Host "Embedding private base: $($system.Id)"
    foreach ($part in @('code','content','meta')) {
        foreach ($file in Get-UFlowTree (Join-Path $base $part)) {
            $relative=$file.FullName.Substring($base.Length+1).Replace('\','/')
            Add-Native $file.FullName ('wiiu/vc-bases/'+$system.Id+'/default/'+$relative) (Get-UFlowHash $file.FullName) $file.Length (1 -shl $ids[$system.Id]) 'b'
        }
    }
}
Copy-Item -LiteralPath (Join-Path $project 'app\data\glass.ttf') -Destination (Join-Path $content 'font.ttf')
$header="#pragma once`n// Generated personal payload. Do not redistribute Nintendo base data.`ninline std::vector<Setup::FileDef> BundledFiles(){ return {`n"+($defs -join "`n")+"`n}; }`n"
[IO.File]::WriteAllText((Join-Path $native 'source\generated_files.hpp'),$header,(New-Object Text.UTF8Encoding($false)))
Write-Host "Native payload ready: $($defs.Count) files. Build native-installer/ with the Wii U toolchain."
