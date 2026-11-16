#!/bin/sh
# Builds LeorixNative-Setup.exe in the repo root from build/leorix.exe using IExpress, without PowerShell.
# Usage (Git Bash): sh installer/build_setup.sh [path/to/leorix.exe]
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
exe=${1:-$root/build/leorix.exe}
[ -f "$exe" ] || { echo "leorix.exe not found at $exe - build the project first." >&2; exit 1; }

stage=$(mktemp -d)   # IExpress wants every payload file in one folder
cp "$exe" "$stage/leorix.exe"
cp "$here/install.ps1" "$here/uninstall.ps1" "$root/LICENSE" "$root/README.md" "$stage/"

w() { cygpath -w "$1"; }
out="$root/LeorixNative-Setup.exe"
sed="$stage/setup.sed"
ps='powershell.exe -NoProfile -ExecutionPolicy Bypass'
printf '%s\r\n' \
  '[Version]' 'Class=IEXPRESS' 'SEDVersion=3' \
  '[Options]' 'PackagePurpose=InstallApp' 'ShowInstallProgramWindow=1' 'HideExtractAnimation=1' \
  'UseLongFileName=1' 'InsideCompressed=0' 'CAB_FixedSize=0' 'CAB_ResvCodeSigning=0' 'RebootMode=N' \
  'InstallPrompt=%InstallPrompt%' 'DisplayLicense=%DisplayLicense%' 'FinishMessage=%FinishMessage%' \
  'TargetName=%TargetName%' 'FriendlyName=%FriendlyName%' 'AppLaunched=%AppLaunched%' \
  'PostInstallCmd=%PostInstallCmd%' 'AdminQuietInstCmd=%AdminQuietInstCmd%' \
  'UserQuietInstCmd=%UserQuietInstCmd%' 'SourceFiles=SourceFiles' \
  '[Strings]' 'InstallPrompt=Install Leorix Native for the current user?' \
  "DisplayLicense=$(w "$stage/LICENSE")" 'FinishMessage=' "TargetName=$(w "$out")" \
  'FriendlyName=Leorix Native Setup' \
  "AppLaunched=$ps -WindowStyle Hidden -File install.ps1" 'PostInstallCmd=<None>' \
  "AdminQuietInstCmd=$ps -File install.ps1 -NoDesktopShortcut" \
  "UserQuietInstCmd=$ps -File install.ps1 -NoDesktopShortcut" \
  'FILE0="leorix.exe"' 'FILE1="install.ps1"' 'FILE2="uninstall.ps1"' 'FILE3="LICENSE"' 'FILE4="README.md"' \
  '[SourceFiles]' "SourceFiles0=$(w "$stage")\\" \
  '[SourceFiles0]' '%FILE0%=' '%FILE1%=' '%FILE2%=' '%FILE3%=' '%FILE4%=' > "$sed"

rm -f "$out"
MSYS2_ARG_CONV_EXCL='*' "$WINDIR/System32/iexpress.exe" /N /Q "$(w "$sed")" || true
rm -rf "$stage"
[ -f "$out" ] || { echo "IExpress failed." >&2; exit 1; }
echo "Built $out ($(( $(stat -c %s "$out") / 1024 )) KB)"
