[CmdletBinding()]
param([string]$CompatSource)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
if(!$CompatSource){$CompatSource=Join-Path (Split-Path (Split-Path $project -Parent) -Parent) 'tooling\sources\vwii-compat-installer-v2.1.1'}
$dest=Join-Path $project 'native-installer\content\console'
$null=New-Item -ItemType Directory -Path $dest -Force
$assets=@{
 'title_cetk.bin'='42C3C11BB99EEC48468E374997400FE062D98F7C59C25074327DFB97BBF52988'
 'title_tmd.bin'='3F19297931A2BFC9A7D79788ABEF67F4D464CFD18C4592898F43B79C8DD9FE74'
 'title_00000000.bin'='A1488ABB67B3D062D791C6944BAC528B9E2A32637EC89EF90734F0645886F685'
 'title_00000001.bin'='B3ABA32582AF2F3F705BF92F281DE0125B27F64B41D77F364E6FA0D3880009EA'
 'wiiflow-vwii.wad'='A569FC78BA42E0BFEE0C4043E1028FE599C30E2D8BA81A3B543AFDB19A6D74D3'
}
foreach($name in $assets.Keys){
    $target=Join-Path $dest $name
    if(!(Test-Path -LiteralPath $target)){
        if($name -eq 'wiiflow-vwii.wad'){
            Invoke-WebRequest 'https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/wiiflow/Default-FIX94v14b-vWii-Forwarder-DWFA.wad' -OutFile $target
        }else{
            $source=Join-Path $CompatSource ('data\'+$name)
            if((Get-FileHash -LiteralPath $source).Hash -ne $assets[$name]){throw "Unexpected source: $name"}
            Copy-Item -LiteralPath $source -Destination $target
        }
    }
    if((Get-FileHash -LiteralPath $target).Hash -ne $assets[$name]){throw "Invalid console asset: $name"}
}
Write-Output 'Verified all integrated vWii console assets.'
