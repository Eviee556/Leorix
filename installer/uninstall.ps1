# Removes the per-user Leorix Native install created by install.ps1.
$ErrorActionPreference = 'SilentlyContinue'
$dest = Join-Path $env:LOCALAPPDATA 'Programs\Leorix Native'
Remove-Item (Join-Path ([Environment]::GetFolderPath('Programs')) 'Leorix Native.lnk') -Force
Remove-Item (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Leorix Native.lnk') -Force
Remove-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\LeorixNative' -Recurse -Force
# This script lives inside $dest, so delete the folder after it exits.
Start-Process cmd.exe -WindowStyle Hidden -ArgumentList "/c timeout /t 2 >nul & rmdir /s /q `"$dest`""
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.MessageBox]::Show('Leorix Native was removed.', 'Leorix Native') | Out-Null
