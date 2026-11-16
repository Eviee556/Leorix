# Leorix Native per-user installer. No admin rights needed.
param([switch]$NoDesktopShortcut)
$ErrorActionPreference = 'Stop'

$src     = Split-Path -Parent $MyInvocation.MyCommand.Path
$dest    = Join-Path $env:LOCALAPPDATA 'Programs\Leorix Native'
$exe     = Join-Path $dest 'leorix.exe'
$startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Leorix Native.lnk'
$desktop   = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Leorix Native.lnk'
$regKey  = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\LeorixNative'

New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item (Join-Path $src 'leorix.exe')    $exe -Force
Copy-Item (Join-Path $src 'uninstall.ps1') (Join-Path $dest 'uninstall.ps1') -Force
foreach ($f in 'LICENSE', 'README.md') {
    $p = Join-Path $src $f
    if (Test-Path $p) { Copy-Item $p (Join-Path $dest $f) -Force }
}

$shell = New-Object -ComObject WScript.Shell
function New-Shortcut($path) {
    $lnk = $shell.CreateShortcut($path)
    $lnk.TargetPath = $exe
    $lnk.WorkingDirectory = $dest
    $lnk.IconLocation = "$exe,0"
    $lnk.Save()
}
New-Shortcut $startMenu
if (-not $NoDesktopShortcut) {
    Add-Type -AssemblyName System.Windows.Forms
    $r = [System.Windows.Forms.MessageBox]::Show('Add a Leorix Native shortcut to the desktop?', 'Leorix Native Setup', 'YesNo', 'Question')
    if ($r -eq 'Yes') { New-Shortcut $desktop }
}

$uninstallCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$dest\uninstall.ps1`""
New-Item -Force $regKey | Out-Null
Set-ItemProperty $regKey DisplayName     'Leorix Native'
Set-ItemProperty $regKey Publisher       'Charlize Pauli'
Set-ItemProperty $regKey DisplayIcon     "$exe,0"
Set-ItemProperty $regKey InstallLocation $dest
Set-ItemProperty $regKey UninstallString $uninstallCmd
Set-ItemProperty $regKey NoModify 1 -Type DWord
Set-ItemProperty $regKey NoRepair 1 -Type DWord
Set-ItemProperty $regKey EstimatedSize ([int]((Get-Item $exe).Length / 1KB)) -Type DWord

Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.MessageBox]::Show("Leorix Native was installed to`n$dest`n`nOpen it from the Start menu.", 'Leorix Native Setup') | Out-Null
