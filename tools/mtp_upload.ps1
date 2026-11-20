# Copies a file to a folder of the console's SD card over MTP and checks it
# arrived whole. Windows' MTP copy won't replace a file, so the old one is
# moved to a folder on the PC first (kept there, to go back to).
#   powershell -File tools/mtp_upload.ps1 KaiVideo.nro
#   powershell -File tools/mtp_upload.ps1 KaiVideo.nro brewstore/apps/kaivideo
param([Parameter(Mandatory = $true)][string]$File, [string]$Dest = 'switch')

$ErrorActionPreference = 'Stop'
$src = (Resolve-Path $File).Path
$name = Split-Path $src -Leaf
$work = Join-Path (Split-Path $src -Parent) 'build\mtp'
$old = Join-Path $work ('old-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$check = Join-Path $work 'check'
foreach ($d in @($old, $check)) {
    Remove-Item -Recurse -Force $d -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $d | Out-Null
}

$shell = New-Object -ComObject Shell.Application
$dev = $shell.Namespace(17).Items() | Where-Object { $_.Name -eq 'Nintendo Switch 2' }
if (-not $dev) { Write-Output 'MTP: the console is not connected'; exit 1 }
$folder = ($dev.GetFolder.Items() | Where-Object { $_.Name -eq 'SD Card' }).GetFolder
foreach ($part in $Dest.Split('/')) {
    $next = $folder.Items() | Where-Object { $_.Name -eq $part -and $_.IsFolder }
    if (-not $next) { Write-Output "MTP: no folder $Dest on the card"; exit 1 }
    $folder = $next.GetFolder
}

function Find-Item { $folder.Items() | Where-Object { $_.Name -eq $name } }
function Wait-Until([scriptblock]$Done) {
    for ($i = 0; $i -lt 120 -and -not (& $Done); $i++) { Start-Sleep -Milliseconds 500 }
}

$existing = Find-Item
if ($existing) {
    $shell.Namespace($old).MoveHere($existing, 16 + 1024)
    Wait-Until { -not (Find-Item) }
    Write-Output "old copy kept in $old"
}
$folder.CopyHere($src, 16 + 1024)
$size = (Get-Item $src).Length
Wait-Until { $it = Find-Item; $it -and $it.ExtendedProperty('System.Size') -eq $size }
$shell.Namespace($check).CopyHere((Find-Item), 16 + 1024)
$back = Join-Path $check $name
Wait-Until { (Test-Path $back) -and (Get-Item $back).Length -eq $size }
$a = (Get-FileHash $src -Algorithm MD5).Hash
$b = if (Test-Path $back) { (Get-FileHash $back -Algorithm MD5).Hash } else { 'missing' }
Write-Output "local   $a"
Write-Output "console $b  ($Dest/$name)"
if ($a -ne $b) { exit 1 }
