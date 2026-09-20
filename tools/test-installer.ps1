[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PackageRoot, [switch]$RealBases)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$ProgressPreference='SilentlyContinue'
$project=Split-Path $PSScriptRoot -Parent
. (Join-Path $project 'installer\Installer.Core.ps1')
$root=Join-Path ([IO.Path]::GetTempPath()) ('uflow-installer-test-' + [guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
function Assert($Condition,[string]$Message) { if (!$Condition) { throw "ASSERT: $Message" } }
function Write-Fixture([string]$Path,[string]$Text) {
    $null=New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($Path)) -Force
    [IO.File]::WriteAllText($Path,$Text,(New-Object Text.UTF8Encoding($false)))
}
function Fails([scriptblock]$Action,[string]$Pattern) {
    $errorText=$null
    try { & $Action | Out-Null } catch { $errorText=$_.Exception.Message }
    Assert ($errorText -and $errorText -like "*$Pattern*") "Expected failure '$Pattern', got '$errorText'"
}
function New-Card([string]$Name) {
    $card=Join-Path $root $Name
    $null=New-Item -ItemType Directory -Path $card
    foreach ($file in @('wiiu/payload.elf','wiiu/environments/aroma/root.rpx',
        'wiiu/environments/aroma/modules/setup/10_wums_loader.rpx',
        'wiiu/environments/aroma/modules/ContentRedirectionModule.wms',
        'wiiu/environments/aroma/modules/WUHBUtilsModule.wms')) { Write-Fixture (Join-Path $card $file) 'Aroma fixture' }
    $card
}
$manifest=Get-Content -LiteralPath (Join-Path $PackageRoot 'manifest.json') -Raw | ConvertFrom-Json
$bases=Join-Path $root 'bases'
foreach ($system in $manifest.Systems) {
    $base=Join-Path $bases ($system.Name + ' [Game] [' + $system.TitleId + ']')
    Write-Fixture (Join-Path $base 'meta/meta.xml') ('<menu><title_id type="hexBinary">' + $system.TitleId + '</title_id></menu>')
    Write-Fixture (Join-Path $base 'code/app.xml') '<app/>'
    Write-Fixture (Join-Path $base 'code/main.rpx') 'test executable'
    Write-Fixture (Join-Path $base 'content/rom.bin') 'test content'
}
$card=New-Card 'card'
$replace=Join-Path $card 'wiiu/environments/aroma/modules/RPXLoadingModule.wms'
$keep=Join-Path $card 'wiiu/apps/uFlow/libraries.cfg'
Write-Fixture $replace 'stock module'
Write-Fixture $keep 'user configuration'
Write-Fixture (Join-Path $card 'games/keep-save.bin') 'user data'
$plan=New-UFlowPlan $PackageRoot $card $bases
Assert ((Get-ChildItem -LiteralPath $card -Directory -Force).Name -notcontains '.uflow-install') 'Preflight must not write'
Assert ($plan.Preserved.Count -eq 1) 'Existing user config preserved'
$report=Invoke-UFlowInstall $plan
$receipt=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
Assert ($receipt.Status -eq 'complete') 'Install completion'
Assert ((Get-Content -LiteralPath $keep -Raw) -eq 'user configuration') 'Settings unchanged'
Assert ((Get-Content -LiteralPath (Join-Path $card 'games/keep-save.bin') -Raw) -eq 'user data') 'Game/save untouched'
$backup=@($receipt.Files | Where-Object Path -eq 'wiiu/environments/aroma/modules/RPXLoadingModule.wms')[0].Backup
Assert ((Get-Content -LiteralPath $backup -Raw) -eq 'stock module') 'Original binary retained'
foreach ($entry in $plan.Entries | Where-Object Action -ne 'preserve') {
    Assert ((Get-UFlowHash (Join-Path $card $entry.Path)) -eq $entry.Sha256) "Readback $($entry.Path)"
}
$second=New-UFlowPlan $PackageRoot $card $bases
Assert ($second.StageBytes -eq 0) 'Idempotent rerun'
Write-Host 'PASS: all backends, 7 base imports, readback, backups, preserved config/saves, idempotent rerun.'

foreach ($path in @('../escape','/absolute','x/../../escape','x:stream','x\y','x/CON.txt','x/../y','x/trailing.','x//y')) {
    Fails { Resolve-UFlowChild $card $path } 'Unsafe path'
}
$outside=Join-Path $root 'outside'; $null=New-Item -ItemType Directory -Path $outside
$junction=Join-Path $card 'linked'; $null=New-Item -ItemType Junction -Path $junction -Target $outside
Fails { Resolve-UFlowChild $card 'linked/new/file' } 'Reparse point'
Write-Fixture (Join-Path $card 'blocked-parent') 'file'
Fails { Resolve-UFlowChild $card 'blocked-parent/child' } 'Parent path is a file'
Fails { Get-UFlowCard 'C:\Windows' } 'single SD drive'
Write-Host 'PASS: traversal, ADS, reserved names, malformed targets and junction escape blocked.'

$conflict=Join-Path $card 'wiiu/vc-bases/ds/default/content/rom.bin'
Write-Fixture $conflict 'different base'
Fails { New-UFlowPlan $PackageRoot $card $bases } 'Different existing VC base'
$wrong=Join-Path $bases 'Nintendo DS [Game] [0005000010195a00]/meta/meta.xml'
Write-Fixture $wrong '<menu><title_id>0005000010100000</title_id></menu>'
Fails { New-UFlowPlan $PackageRoot $card $bases } 'Wrong title ID'
Write-Fixture $wrong '<menu><title_id>0005000010195a00</title_id></menu>'
$missing=Join-Path $root 'missing-bases'; $null=New-Item -ItemType Directory -Path $missing
Fails { New-UFlowPlan $PackageRoot $card $missing } 'Missing/ambiguous'
Write-Host 'PASS: wrong/missing bases and conflicting installed base refused before writes.'

$fresh=New-Card 'staging-failure'
$plan=New-UFlowPlan $PackageRoot $fresh $bases
$plan.Entries[0].Source=Join-Path $root 'nonexistent-source'
Fails { Invoke-UFlowInstall $plan } 'Install stopped'
Assert (!(Test-Path -LiteralPath (Join-Path $fresh 'wiiu/apps/uFlow/uFlow.wuhb'))) 'Staging failure must not publish app'
Write-Host 'PASS: staging failure leaves live installation untouched; no automatic rollback.'

# A component selection is a clean install recipe, not removal of unchecked files.
$optional=New-Card 'optional'
$minimal=New-UFlowPlan $PackageRoot $optional (Join-Path $root 'does-not-exist') -Components @()
Assert ($minimal.Entries.Count -eq 7) 'Core-only app, four configs and two instructions'
Assert ($minimal.Directories.Count -eq 0) 'No unselected content folders'
Assert ((@($minimal.Entries | Where-Object Path -like '*libraries.cfg')[0]).Text -eq '') 'Core-only intentionally empty libraries'
$minimalReport=Invoke-UFlowInstall $minimal
Assert ((Get-Item -LiteralPath (Join-Path $optional 'wiiu/apps/uFlow/libraries.cfg')).Length -eq 0) 'Empty config persisted'
Assert ((Get-Content -LiteralPath (Join-Path $optional 'wiiu/apps/uFlow/uflow.cfg') -Raw).Contains('show_installed=0')) 'Unselected installed Wii U games hidden'
$onlyNoods=New-UFlowPlan $PackageRoot (New-Card 'noods-only') (Join-Path $root 'does-not-exist') -Components @('noods')
Assert (@($onlyNoods.Entries | Where-Object Path -like '*vc-bases*').Count -eq 0) 'NooDS needs no VC base'
Assert (@($onlyNoods.Entries | Where-Object Path -like '*RPXLoadingModule*').Count -eq 0) 'NooDS does not replace raw backend'
$dsConfig=@($onlyNoods.Entries | Where-Object Path -like '*libraries.cfg')[0].Text
Assert ($dsConfig.Contains("`tnoods`t") -and !$dsConfig.Contains('ds-vc')) 'DS NooDS default assigned'
$one=New-UFlowPlan $PackageRoot (New-Card 'nes-only') $bases -Components @('nes')
Assert (@($one.Entries | Where-Object Path -like '*vc-bases/nes/*').Count -eq 4) 'Only selected NES base'
Assert (@($one.Entries | Where-Object { $_.Path -like '*vc-bases/*' -and $_.Path -notlike '*vc-bases/nes/*' }).Count -eq 0) 'No unrelated VC bases'
Assert (@($one.Entries | Where-Object Path -like '*RPXLoadingModule*').Count -eq 1) 'VC adds shared raw backend'
Assert (@($one.Entries | Where-Object Path -like '*99_autoboot*').Count -eq 0) 'Startup optional'
$gc=New-UFlowPlan $PackageRoot (New-Card 'gc-only') '' -Components @('gamecube')
Assert (@($gc.Entries | Where-Object Path -eq 'apps/nintendont/boot.dol').Count -eq 1) 'GC installs Nintendont'
Assert (@($gc.Entries | Where-Object Path -eq 'apps/wiiflow/boot.dol').Count -eq 1) 'GC installs shared WiiFlow bridge'
Assert (@($gc.Entries | Where-Object Path -like '*compat_installer*').Count -eq 0) 'First-time tools optional'
Fails { New-UFlowPlan $PackageRoot $optional '' -Components @('unknown') } 'Unknown component'
Write-Host 'PASS: optional core-only, NooDS without bases, single VC base, shared dependencies and optional tools/startup.'

# Missing/corrupt selected package asset and duplicate manifest entries fail preflight.
$badPackage=Join-Path $root 'bad-package'
$null=New-Item -ItemType Directory -Path $badPackage
Copy-Item -LiteralPath (Join-Path $PackageRoot 'payload') -Destination $badPackage -Recurse
Copy-Item -LiteralPath (Join-Path $PackageRoot 'manifest.json') -Destination $badPackage
Write-Fixture (Join-Path $badPackage 'payload/wiiu/apps/uFlow/uFlow.wuhb') 'corrupt'
Fails { New-UFlowPlan $badPackage $optional '' -Components @() } 'Package damaged'
Copy-Item -LiteralPath (Join-Path $PackageRoot 'payload/wiiu/apps/uFlow/uFlow.wuhb') -Destination (Join-Path $badPackage 'payload/wiiu/apps/uFlow/uFlow.wuhb') -Force
$duplicate=Get-Content -LiteralPath (Join-Path $badPackage 'manifest.json') -Raw | ConvertFrom-Json
$duplicate.Files += $duplicate.Files[0]
Write-Fixture (Join-Path $badPackage 'manifest.json') ($duplicate | ConvertTo-Json -Depth 8)
Fails { New-UFlowPlan $badPackage $optional '' -Components @() } 'Duplicate target'
Write-Host 'PASS: damaged package and duplicate targets rejected before writes.'

# Test actual binaries + actual decrypted base set on a disposable local directory.
# This never accesses a card, formats anything, or runs any console executable.
if ($RealBases) {
    $realCard=New-Card 'real-base-smoke'
    $real=New-UFlowPlan $PackageRoot $realCard 'D:\WiiU'
    Write-Host ("Real-base smoke: {0} files / {1:N0} MB" -f $real.Entries.Count,($real.StageBytes/1MB))
    $realReport=Invoke-UFlowInstall $real
    Assert ((Get-Content -LiteralPath $realReport -Raw | ConvertFrom-Json).Status -eq 'complete') 'Real base install'
    $rerun=New-UFlowPlan $PackageRoot $realCard 'D:\WiiU'
    Assert ($rerun.StageBytes -eq 0) 'Real base idempotence'
    Write-Host "PASS: real seven-system base set and all backend files installed/readback verified: $realReport"
}
Write-Host "ALL INSTALLER TESTS PASSED. Disposable evidence retained: $root"
