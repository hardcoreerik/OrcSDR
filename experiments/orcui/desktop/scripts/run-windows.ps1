[CmdletBinding()]
param(
 [ValidateSet('waveshare','tab5','round480','round360','ultrawide','portrait','compact')]
 [string]$Preset='waveshare',
 [string]$MsYsRoot='C:\msys64'
)
$ErrorActionPreference='Stop'
$desktop=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exe=Join-Path $desktop 'build\orcui_desktop.exe'
if(!(Test-Path $exe)){throw "Simulator not built: $exe. Run install-windows.ps1"}
$env:PATH="$(Join-Path $MsYsRoot 'ucrt64\bin');$env:PATH"
$log=Join-Path $desktop 'orcui_desktop.log'
"OrcUI simulator $(Get-Date -Format o), preset=$Preset" | Set-Content $log
$previousErrorAction=$ErrorActionPreference
$ErrorActionPreference='Continue'
& $exe --preset $Preset 2>&1 | Tee-Object -FilePath $log -Append
$exit=$LASTEXITCODE
$ErrorActionPreference=$previousErrorAction
if($exit -ne 0){throw "Simulator exit $exit; log: $log"}
