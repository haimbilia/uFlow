param([string]$HostAddress='192.168.1.251',[switch]$StageOnly,[switch]$UseStaged)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$files=@(
    @{Local='app/uFlow.wuhb';Remote='/fs/vol/external01/wiiu/apps/uFlow/uFlow.wuhb'},
    @{Local='module/RPXLoadingModule.wms';Remote='/fs/vol/external01/wiiu/environments/aroma/modules/RPXLoadingModule.wms'},
    @{Local='payload/00_mocha.rpx';Remote='/fs/vol/external01/wiiu/environments/aroma/modules/setup/00_mocha.rpx'}
)
# Verify every staged file before replacing any installed component.
foreach($file in $files){
    $local=Join-Path $project $file.Local
    $url="ftp://$HostAddress$($file.Remote).usb-test-upload"
    if(!$UseStaged){
        curl.exe -sS --fail --connect-timeout 10 --max-time 240 --limit-rate 1M --upload-file $local $url
        if($LASTEXITCODE -ne 0){throw "Upload failed: $($file.Local)"}
    }
    $readback=Join-Path $project ('artifacts/usb-raw-readback-'+[IO.Path]::GetFileName($local))
    curl.exe -sS --fail --connect-timeout 10 --max-time 240 --output $readback $url
    if($LASTEXITCODE -ne 0){throw "Readback failed: $($file.Local)"}
    if((Get-FileHash $local).Hash -ne (Get-FileHash $readback).Hash){throw "Hash mismatch: $($file.Local)"}
    Write-Output "Verified $($file.Local): $((Get-FileHash $local).Hash)"
}
if($StageOnly){return}
foreach($file in $files){
    curl.exe -sS --fail --connect-timeout 10 --max-time 30 --quote "DELE $($file.Remote)" --quote "RNFR $($file.Remote).usb-test-upload" --quote "RNTO $($file.Remote)" "ftp://$HostAddress/fs/vol/external01/wiiu/" --output NUL
    if($LASTEXITCODE -ne 0){throw "Activation failed: $($file.Remote). Do not reboot until deployment completes."}
    Write-Output "Installed $($file.Remote)"
}
