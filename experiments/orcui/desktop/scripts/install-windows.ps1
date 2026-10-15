# OrcUI installer for Windows local Codex: separate from ESP-IDF/Tab5.
[CmdletBinding()]
param(
 [switch]$NoLaunch,
 [ValidateSet('waveshare','tab5','round480','round360','ultrawide','portrait','compact')]
 [string]$Preset='waveshare',
 [string]$MsYsRoot='C:\msys64'
)
$ErrorActionPreference='Stop'
$desktop=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$log=Join-Path $desktop 'install-windows.log'
"OrcUI desktop install $(Get-Date -Format o)" | Set-Content $log
function Invoke-Msys([string]$Command,[string]$Stage){
 $bash=Join-Path $MsYsRoot 'usr\bin\bash.exe'
 if(!(Test-Path $bash)){throw "MSYS2 unavailable at $bash"}
 $env:MSYSTEM='UCRT64';$env:CHERE_INVOKING='1'
 "==== $Stage ====" | Tee-Object -FilePath $log -Append
 & $bash -lc "$Command 2>&1" | Tee-Object -FilePath $log -Append
 if($LASTEXITCODE -ne 0){throw "$Stage failed: exit $LASTEXITCODE; see $log"}
}
if(!(Test-Path (Join-Path $MsYsRoot 'usr\bin\bash.exe'))){
 if(!(Get-Command winget -ErrorAction SilentlyContinue)){
  throw 'Install MSYS2 from https://www.msys2.org first; winget not available.'
 }
 & winget install --id MSYS2.MSYS2 --exact --source winget --accept-source-agreements --accept-package-agreements
 if($LASTEXITCODE -ne 0){throw 'MSYS2 winget install failed'}
}
Invoke-Msys 'pacman -Syu --noconfirm' 'Update MSYS2 packages'
Invoke-Msys 'pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-pkgconf git' 'Install UCRT64 prerequisites'
$env:ORCUI_DESKTOP_DIR=$desktop
Invoke-Msys 'set -e; cd "$(cygpath -u "$ORCUI_DESKTOP_DIR")"; cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON; cmake --build build --parallel 4; ctest --test-dir build --output-on-failure' 'Build and smoke test'
Write-Host "OrcUI ready. Installation log: $log"
if(!$NoLaunch){ & (Join-Path $PSScriptRoot 'run-windows.ps1') -Preset $Preset -MsYsRoot $MsYsRoot }
