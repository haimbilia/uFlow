$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=Split-Path $PSScriptRoot -Parent
. (Join-Path $project 'installer\Installer.Core.ps1')
. (Join-Path $project 'installer\Installer.Checklist.ps1')
$ui=New-UFlowChecklist
try {
    if ($ui.List.Items.Count -ne 13 -or $ui.List.CheckedItems.Count -ne 13) { throw 'Checklist items/defaults incorrect' }
    # Invoke the click events directly without showing an interactive window.
    $click=[Windows.Forms.Control].GetMethod('OnClick',[Reflection.BindingFlags]'Instance,NonPublic')
    $null=$click.Invoke($ui.ClearAll,@([EventArgs]::Empty))
    if ($ui.List.CheckedItems.Count -ne 0) { throw 'Clear all failed' }
    $null=$click.Invoke($ui.SelectAll,@([EventArgs]::Empty))
    if ($ui.List.CheckedItems.Count -ne 13) { throw 'Select all failed' }
    # Off-screen, absent from taskbar: exercise native layout without displaying
    # an interactive test window to the user.
    $ui.Form.ShowInTaskbar=$false
    $ui.Form.StartPosition='Manual'
    $ui.Form.Location=New-Object Drawing.Point(-32000,-32000)
    $ui.Form.Show()
    [Windows.Forms.Application]::DoEvents()
    $bitmap=New-Object Drawing.Bitmap($ui.Form.Width,$ui.Form.Height)
    try {
        $ui.Form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$bitmap.Width,$bitmap.Height)))
        $out=Join-Path $project 'artifacts\installer-checklist-preview.png'
        $bitmap.Save($out,[Drawing.Imaging.ImageFormat]::Png)
        Write-Host "PASS: 13 checklist items, select/clear actions. Preview: $out"
    } finally { $bitmap.Dispose() }
} finally { $ui.Form.Dispose() }
