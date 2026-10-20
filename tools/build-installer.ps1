[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project = Split-Path $PSScriptRoot -Parent
$workspace = Split-Path (Split-Path $project -Parent) -Parent
$sources = Join-Path $workspace 'tooling\sources'
$downloads = Join-Path $workspace 'tooling\downloads\vwii-setup-20260909'
. (Join-Path $project 'installer\Installer.Core.ps1')
if (!$OutputDirectory) { $OutputDirectory = Join-Path $project ('dist\uFlow-Setup-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output already exists. Use a new directory; no old builds are removed.' }
$null = New-Item -ItemType Directory -Path $OutputDirectory
$payload = Join-Path $OutputDirectory 'payload'
$null = New-Item -ItemType Directory -Path $payload
$files = New-Object 'Collections.Generic.List[object]'
$directories = New-Object 'Collections.Generic.List[string]'
$provenance = New-Object 'Collections.Generic.List[string]'

function Add-Asset([string]$Source, [string]$Path, [string]$Mode = 'replace') {
    $dest = Resolve-UFlowChild $payload $Path
    if (Test-Path -LiteralPath $dest) { throw "Duplicate payload: $Path" }
    $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($dest)) -Force
    $hash = Get-UFlowHash $Source
    [IO.File]::Copy($Source,$dest,$false)
    if ((Get-UFlowHash $dest) -ne $hash) { throw "Package copy failed: $Source" }
    $files.Add([pscustomobject]@{Path=$Path; Bytes=(Get-Item -LiteralPath $dest).Length; Sha256=$hash; Mode=$Mode})
    $provenance.Add("$Path <- $Source / SHA256 $hash")
}
function Add-Text([string]$Path, [string]$Text, [string]$Mode = 'keep') {
    $dest = Resolve-UFlowChild $payload $Path
    if (Test-Path -LiteralPath $dest) { throw "Duplicate payload: $Path" }
    $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($dest)) -Force
    [IO.File]::WriteAllText($dest,$Text,(New-Object Text.UTF8Encoding($false)))
    $files.Add([pscustomobject]@{Path=$Path; Bytes=(Get-Item -LiteralPath $dest).Length; Sha256=(Get-UFlowHash $dest); Mode=$Mode})
}
function Add-Tree([string]$Source, [string]$Prefix, [string]$Mode = 'keep') {
    foreach ($file in Get-UFlowTree $Source) {
        Add-Asset $file.FullName ($Prefix + '/' + $file.FullName.Substring($Source.Length+1).Replace('\','/')) $Mode
    }
}
function Assert-Marker([string]$Path, [string]$Marker) {
    if (![Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($Path)).Contains($Marker)) { throw "Missing $Marker in $Path" }
}
function Assert-Hash([string]$Path, [string]$Hash) {
    if ((Get-UFlowHash $Path) -ne $Hash) { throw "Pinned file differs: $Path" }
}

Assert-Marker (Join-Path $project 'app\uFlow.wuhb') 'WUHB'
Assert-Marker (Join-Path $project 'emulators\NooDS\noods.wuhb') 'WUHB'
$wf = Join-Path $sources 'wiiflow-lite\out\boot.dol'
foreach ($marker in @('UFLOW_WII_PATH_BACKEND_V1','UFLOW_GC_PATH_BACKEND_V1','UFLOW_LAUNCHER_SELECT_V1')) { Assert-Marker $wf $marker }
Assert-Marker (Join-Path $sources 'usbloadergx\boot.dol') 'UFLOW_USBLOADER_PATH_V1'
Assert-Hash (Join-Path $sources 'nintendont\loader\loader.dol') 'e6aaec18db139da234fac29036a3d00f4800ba2bff613b733ababb5dcce9e197'
Assert-Hash (Join-Path $downloads 'compat\compat_installer.wuhb') '4b1ec058cba4a19be009d6dabd1694842d57cc75426009cb4dae3feab3950d0e'
Assert-Hash (Join-Path $downloads 'wiiflow-channel-installer-1.1\apps\wiiflow_channel_installer\boot.dol') 'dd7a0a9db259900ad4bea942f7f739da5f73bc24e817da0ae58a34bd9898f80f'
$d2x = Join-Path $downloads 'd2x\apps\d2x-cios-installer-vwii'
if ((Get-FileHash -LiteralPath (Join-Path $d2x 'boot.dol') -Algorithm MD5).Hash -ne 'c6aa7cee75448a3860931a762f157aaf') { throw 'Unexpected vWii d2x installer' }
if (!(Get-Content -LiteralPath (Join-Path $d2x 'ciosmaps.xml') -Raw).Contains('d2x-v11-beta3-vWii')) { throw 'Missing vWii cIOS mappings' }

Add-Asset (Join-Path $project 'app\uFlow.wuhb') 'wiiu/apps/uFlow/uFlow.wuhb'
Add-Asset (Join-Path $project 'module\RPXLoadingModule.wms') 'wiiu/environments/aroma/modules/RPXLoadingModule.wms'
Add-Asset (Join-Path $project 'kernel\uFlowRawKernel.wms') 'wiiu/environments/aroma/modules/uFlowRawKernel.wms'
Add-Asset (Join-Path $project 'payload\00_mocha.rpx') 'wiiu/environments/aroma/modules/setup/00_mocha.rpx'
Add-Asset (Join-Path $sources 'AutobootModule\99_autoboot.rpx') 'wiiu/environments/aroma/modules/setup/99_autoboot.rpx'
Add-Asset $wf 'apps/wiiflow/boot.dol'
foreach ($name in @('meta.xml','icon.png')) {
    Add-Asset (Join-Path $sources "wiiflow-lite\wii\apps\wiiflow\$name") "apps/wiiflow/$name"
    Add-Asset (Join-Path $sources "usbloadergx\HBC\$name") "apps/usbloader_gx/$name"
    Add-Asset (Join-Path $sources "nintendont\nintendont\$name") "apps/nintendont/$name"
}
Add-Tree (Join-Path $downloads 'wiiflow-v5.6.2\wiiflow') 'wiiflow'
Add-Text 'apps/wiiflow/wiiflow_lite.ini' "[DEBUG]`nsd_write_log=no`n"
Add-Asset (Join-Path $sources 'usbloadergx\boot.dol') 'apps/usbloader_gx/boot.dol'
Add-Asset (Join-Path $sources 'nintendont\loader\loader.dol') 'apps/nintendont/boot.dol'
Add-Asset (Join-Path $project 'emulators\NooDS\noods.wuhb') 'wiiu/apps/noods/noods.wuhb'
Add-Asset (Join-Path $project 'launcher-modules\noods-capability.txt') 'wiiu/apps/noods/uflow-api.txt'
Add-Asset (Join-Path $project 'launcher-modules\noods.ufmod') 'wiiu/apps/uFlow/launchers/noods.ufmod'
# First-time tools: staged only, never launched by this installer.
Add-Asset (Join-Path $downloads 'compat\compat_installer.wuhb') 'wiiu/apps/compat_installer.wuhb'
Add-Tree $d2x 'apps/d2x-cios-installer-vwii' 'replace'
Add-Tree (Join-Path $downloads 'wiiflow-channel-installer-1.1\apps\wiiflow_channel_installer') 'apps/wiiflow_channel_installer' 'replace'

$systems = @(
    @{Id='ds'; Name='Nintendo DS'; TitleId='0005000010195a00'; Extensions='.nds,.srl'},
    @{Id='nes'; Name='NES'; TitleId='0005000010133100'; Extensions='.nes'},
    @{Id='snes'; Name='SNES'; TitleId='0005000010115300'; Extensions='.sfc,.smc'},
    @{Id='gba'; Name='GBA'; TitleId='000500001015e400'; Extensions='.gba'},
    @{Id='n64'; Name='N64'; TitleId='0005000010199200'; Extensions='.z64,.v64,.n64'},
    @{Id='pce'; Name='PC Engine'; TitleId='0005000010166d00'; Extensions='.pce'},
    @{Id='msx'; Name='MSX'; TitleId='0005000010195400'; Extensions='.rom,.mx1,.mx2'}
)
$libraries = New-Object 'Collections.Generic.List[string]'
$content = New-Object 'Collections.Generic.List[string]'
$media = New-Object 'Collections.Generic.List[string]'
$libraries.Add("wiiu`tWii U`twiiu`twiiflow`t.rpx`t1`t0`tnative`t135`t190`t14")
$libraries.Add("wii`tWii`twii`twiiflow`t.wbfs`t1`t1`twiiflow`t135`t190`t14")
$libraries.Add("gamecube`tGameCube`tgamecube`twiiflow`t.iso`t1`t2`tnintendont`t135`t190`t14")
foreach ($row in @(@('wiiu','wiiu/raw-games','native'), @('wiiu','wiiu/games','native'), @('wii','wbfs','wiiflow'), @('gamecube','games','nintendont'))) {
    $content.Add(($row[0],'1','3',('fs:/vol/external01/' + $row[1]),'wiiflow',$row[0],$row[2]) -join "`t")
    $directories.Add($row[1])
}
$order=3
foreach ($system in $systems) {
    $id=$system.Id
    Add-Asset (Join-Path $project "launcher-modules\$id-vc.ufmod") "wiiu/apps/uFlow/launchers/$id-vc.ufmod"
    $profile = Get-Content -LiteralPath (Join-Path $project "launcher-modules\$id-vc-example.cfg") -Raw
    $profile = [regex]::Replace($profile,'(?m)^base=[^\r\n]*',"base=wiiu/vc-bases/$id/default")
    Add-Text "wiiu/apps/uFlow/launchers/$id-vc.cfg" $profile
    $width = if ($id -eq 'snes') { '178' } else { '135' }
    $height = if ($id -eq 'ds') { '122' } elseif ($id -eq 'snes') { '125' } else { '190' }
    $thickness = if ($id -eq 'ds') { '15' } elseif ($id -eq 'snes') { '25' } else { '14' }
    $libraries.Add(($id,$system.Name,'files','wiiflow',$system.Extensions,'1',"$order","$id-vc",$width,$height,$thickness) -join "`t")
    $content.Add(('files','1','3',"fs:/vol/external01/wiiu/roms/$id",'wiiflow',$id,"$id-vc") -join "`t")
    $directories.Add("wiiu/roms/$id"); $order++
}
foreach ($id in @('wiiu','wii','gamecube') + @($systems | ForEach-Object { $_.Id })) {
    foreach ($kind in @('covers','fullcovers','backgrounds','logos','previews','metadata')) {
        $relative="wiiu/apps/uFlow/media/$id/$kind"
        $directories.Add($relative)
        $media.Add(($kind,"id:$id",'1','1',"fs:/vol/external01/$relative",'1000','1','70','','100') -join "`t")
    }
}
Add-Text 'wiiu/apps/uFlow/libraries.cfg' (($libraries -join "`n") + "`n")
Add-Text 'wiiu/apps/uFlow/uflow.cfg' "show_installed=1`nshow_raw_wiiu=1`nshow_wii=1`nshow_gamecube=1`n"
Add-Text 'wiiu/apps/uFlow/content_sources.cfg' (($content -join "`n") + "`n")
Add-Text 'wiiu/apps/uFlow/media_sources.cfg' (($media -join "`n") + "`n")
foreach ($name in @('README.txt','FIRST-RUN.txt')) {
    Add-Asset (Join-Path $project "installer\$name") "wiiu/apps/uFlow/setup/$name"
}
foreach ($name in @('Install-uFlow.cmd','Install-uFlow.ps1','Installer.Core.ps1','Installer.Checklist.ps1','README.txt','FIRST-RUN.txt')) {
    Copy-Item -LiteralPath (Join-Path $project "installer\$name") -Destination (Join-Path $OutputDirectory $name)
}
$manifest = [ordered]@{Version=1; Created=(Get-Date -Format o); Files=@($files.ToArray()); Directories=@($directories.ToArray()); Systems=$systems}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'manifest.json'),($manifest | ConvertTo-Json -Depth 7),(New-Object Text.UTF8Encoding($false)))
$notices = @'
Personal/local test bundle. No Nintendo games, keys, BIOS or IOS bases included.
VC bases are imported from the user's chosen local directory at install time.
Upstream licenses and source remain applicable to their respective components.
This is not a signed public release. Do not redistribute without packaging the
complete corresponding sources/licenses, including the local integration changes.

uFlow local app/modules/payload sources: D:/Projects/WiiU/projects/uFlow/
Patched WiiFlow: https://github.com/Fledge68/WiiFlow_Lite (v5.6.2)
Patched USB Loader GX: https://github.com/wiidev/usbloadergx
Nintendont: https://github.com/FIX94/Nintendont (0f69235b99099ef2851a5f6b7b0e349c572237e8)
Patched NooDS: https://github.com/Hydr8gon/NooDS (4cb1439c330248b26cdf5f8dbcfa4e5ac53dcda0)
Patched Autoboot: https://github.com/wiiu-env/AutobootModule
Patched RPX loading: https://github.com/wiiu-env/RPXLoadingModule
Patched Mocha: https://github.com/wiiu-env/MochaPayload
Homebrew Channel installer: https://github.com/Xpl0itU/vwii-compat-installer/releases/tag/v1.6
Homebrew Channel source: https://github.com/FIX94/hbc
d2x: https://github.com/wiidev/d2x-cios
d2x vWii installer: https://oscwii.org/library/app/d2x-cios-installer-vwii
WiiFlow Channel Installer: https://sites.google.com/view/wiiflow-wiki/forwarder-channels

Exact source paths and binary hashes used in this bundle:
'@
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'SOURCES.txt'),$notices + "`r`n" + ($provenance -join "`r`n"))
$licenseDir = Join-Path $OutputDirectory 'licenses'
$null = New-Item -ItemType Directory -Path $licenseDir
foreach ($entry in @(@('NooDS', (Join-Path $project 'emulators\NooDS\LICENSE')), @('AutobootModule',(Join-Path $sources 'AutobootModule\LICENSE')))) {
    Copy-Item -LiteralPath $entry[1] -Destination (Join-Path $licenseDir ($entry[0] + '.txt'))
}
Write-Output "PACKAGE READY: $OutputDirectory"
Write-Output ("{0} files, {1} library/media directories, 7 local VC bases imported at install time." -f $files.Count,$directories.Count)
