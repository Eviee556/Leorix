# Builds LeorixNative-Setup.exe in the repo root from build\leorix.exe using IExpress (ships with Windows).
param([string]$Exe = '')
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $here
if (-not $Exe) { $Exe = Join-Path $root 'build\leorix.exe' }
if (-not (Test-Path $Exe)) { throw "leorix.exe not found at $Exe - build the project first." }

# IExpress wants every payload file in one folder.
$stage = Join-Path $env:TEMP 'leorix-setup-stage'
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $stage | Out-Null
Copy-Item $Exe (Join-Path $stage 'leorix.exe')
Copy-Item (Join-Path $here 'install.ps1'), (Join-Path $here 'uninstall.ps1') $stage
Copy-Item (Join-Path $root 'LICENSE'), (Join-Path $root 'README.md') $stage

$out = Join-Path $root 'LeorixNative-Setup.exe'
$sed = Join-Path $stage 'setup.sed'
$files = 'leorix.exe', 'install.ps1', 'uninstall.ps1', 'LICENSE', 'README.md'
$strings = for ($i = 0; $i -lt $files.Count; $i++) { "FILE$i=`"$($files[$i])`"" }
$list    = for ($i = 0; $i -lt $files.Count; $i++) { "%FILE$i%=" }

@"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=Install Leorix Native for the current user?
DisplayLicense=$stage\LICENSE
FinishMessage=
TargetName=$out
FriendlyName=Leorix Native Setup
AppLaunched=powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File install.ps1
PostInstallCmd=<None>
AdminQuietInstCmd=powershell.exe -NoProfile -ExecutionPolicy Bypass -File install.ps1 -NoDesktopShortcut
UserQuietInstCmd=powershell.exe -NoProfile -ExecutionPolicy Bypass -File install.ps1 -NoDesktopShortcut
$($strings -join "`r`n")
[SourceFiles]
SourceFiles0=$stage\
[SourceFiles0]
$($list -join "`r`n")
"@ | Set-Content $sed -Encoding ASCII

Remove-Item $out -Force -ErrorAction SilentlyContinue
$p = Start-Process "$env:WINDIR\System32\iexpress.exe" -ArgumentList '/N', '/Q', $sed -Wait -PassThru
if (-not (Test-Path $out)) { throw "IExpress failed (exit $($p.ExitCode))." }
Write-Host "Built $out ($([math]::Round((Get-Item $out).Length / 1MB, 1)) MB)"
