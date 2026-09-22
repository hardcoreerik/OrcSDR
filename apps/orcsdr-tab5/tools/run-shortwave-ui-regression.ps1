<#
.SYNOPSIS
Runs the repeatable Shortwave dashboard and UI regression suite.

.DESCRIPTION
The default run executes the focused host checks and the shared Tab5 harness
self-check. Add -Build for a native ESP-IDF build, -Device for an authenticated
reset plus on-device UI regression, and -CapturePath for recorded-audio A/B.
#>
[CmdletBinding()]
param(
  [switch]$SelfCheck,
  [switch]$Build,
  [switch]$Device,
  [switch]$ResetDevice,
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port = 'COM17',
  [string]$CapturePath,
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4',
  [string]$BuildDirectory = 'build-native-shortwave',
  [string]$PairingKeyPath = (Join-Path $PSScriptRoot '..\..\..\.orclink\ui-doc.key')
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$sharedRunner = Join-Path $PSScriptRoot 'run-tab5-ui-regression.ps1'
$required = @(
  $sharedRunner,
  (Join-Path $repo 'apps\orcsdr-tab5\tests\shortwave_model_host_test.cpp'),
  (Join-Path $repo 'tests\shortwave_dashboard_state_tests.cpp'),
  (Join-Path $repo 'tests\shortwave_library_tests.cpp'),
  (Join-Path $repo 'tests\shortwave_library_io_tests.cpp'),
  (Join-Path $repo 'tests\shortwave_hunt_tests.cpp'),
  (Join-Path $repo 'tests\shortwave_audio_dsp_tests.cpp')
)
foreach ($path in $required) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Required Shortwave regression input is missing: $path"
  }
}
if ($SelfCheck) {
  Write-Host 'SHORTWAVE_UI_RUNNER_SELF_CHECK pass=1'
  exit 0
}

function ConvertTo-WslPath([string]$Path) {
  $resolved = (Resolve-Path -LiteralPath $Path).Path
  $converted = (& wsl.exe wslpath -a ($resolved -replace '\\', '/') 2>&1)
  if ($LASTEXITCODE -ne 0) { throw "Could not convert path for WSL: $converted" }
  return ($converted | Select-Object -Last 1).Trim()
}

function Quote-Bash([string]$Value) {
  if ($Value.Contains("'")) { throw "WSL path contains an unsupported apostrophe: $Value" }
  return "'" + $Value + "'"
}

function Invoke-WslCheck([string]$Name, [string]$Command) {
  Write-Host "SHORTWAVE_UI_TEST begin=$Name"
  & wsl.exe bash -lc $Command
  if ($LASTEXITCODE -ne 0) { throw "Shortwave check failed: $Name" }
  Write-Host "SHORTWAVE_UI_TEST pass=$Name"
}

$wslRepo = ConvertTo-WslPath $repo
$testDir = '/tmp/orcsdr-shortwave-tests'
$prefix = "set -e; cd $(Quote-Bash $wslRepo); mkdir -p $testDir; " +
          'g++ -std=c++17 -Iapps/orcsdr-tab5/ui '
Invoke-WslCheck 'model' ($prefix +
  "apps/orcsdr-tab5/ui/shortwave_model.cpp apps/orcsdr-tab5/tests/shortwave_model_host_test.cpp -o $testDir/model && $testDir/model")
Invoke-WslCheck 'dashboard_state' ($prefix +
  "apps/orcsdr-tab5/ui/shortwave_dashboard_state.cpp tests/shortwave_dashboard_state_tests.cpp -o $testDir/state && $testDir/state")
Invoke-WslCheck 'library' ($prefix +
  "apps/orcsdr-tab5/ui/shortwave_model.cpp apps/orcsdr-tab5/ui/shortwave_library.cpp tests/shortwave_library_tests.cpp -o $testDir/library && $testDir/library")
Invoke-WslCheck 'library_io' ($prefix +
  "apps/orcsdr-tab5/ui/shortwave_model.cpp apps/orcsdr-tab5/ui/shortwave_library.cpp apps/orcsdr-tab5/ui/shortwave_library_io.cpp tests/shortwave_library_io_tests.cpp -o $testDir/library_io && $testDir/library_io")
Invoke-WslCheck 'hunt' ($prefix +
  "apps/orcsdr-tab5/ui/shortwave_model.cpp apps/orcsdr-tab5/ui/scan_engine.cpp apps/orcsdr-tab5/ui/shortwave_hunt.cpp tests/shortwave_hunt_tests.cpp -o $testDir/hunt && $testDir/hunt")

$dspCommand = $prefix +
  "-pthread -fsanitize=thread -no-pie apps/orcsdr-tab5/ui/shortwave_audio_dsp.cpp tests/shortwave_audio_dsp_tests.cpp -o $testDir/dsp && setarch `$(uname -m) -R $testDir/dsp"
if ($CapturePath) {
  if (-not (Test-Path -LiteralPath $CapturePath -PathType Leaf)) {
    throw "Shortwave capture does not exist: $CapturePath"
  }
  $dspCommand += ' ' + (Quote-Bash (ConvertTo-WslPath $CapturePath))
}
Invoke-WslCheck 'audio_dsp' $dspCommand

& $sharedRunner -SelfCheck
if ($LASTEXITCODE -ne 0) { throw 'Shared Tab5 regression harness self-check failed.' }
Write-Host 'SHORTWAVE_UI_TEST pass=shared_harness'

if ($Build) {
  $app = Join-Path $repo 'apps\orcsdr-tab5'
  $command = "call `"$IdfPath\export.bat`" >nul && idf.py -B `"$BuildDirectory`" build"
  Push-Location $app
  try {
    cmd.exe /c $command
    if ($LASTEXITCODE -ne 0) { throw "Native firmware build failed: $LASTEXITCODE" }
  } finally {
    Pop-Location
  }
  $binary = Join-Path $app "$BuildDirectory\orcsdr_tab5.bin"
  $item = Get-Item -LiteralPath $binary
  $hash = Get-FileHash -LiteralPath $binary -Algorithm SHA256
  Write-Host "SHORTWAVE_UI_BUILD pass=1 bytes=$($item.Length) sha256=$($hash.Hash) artifact=$($item.FullName)"
}

if ($Device) {
  $deviceArguments = @{
    Port = $Port
    Run = $true
    PairingKeyPath = $PairingKeyPath
  }
  if ($ResetDevice) { $deviceArguments.ResetDevice = $true }
  & $sharedRunner @deviceArguments
  if ($LASTEXITCODE -ne 0) { throw 'On-device UI regression failed.' }
  Write-Host "SHORTWAVE_UI_DEVICE pass=1 port=$Port"
}

Write-Host 'SHORTWAVE_UI_REGRESSION pass=1'
