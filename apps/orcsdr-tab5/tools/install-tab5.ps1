param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port,
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4'
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $IdfPath 'export.ps1'))) {
  throw "ESP-IDF 5.5.4 is not installed at $IdfPath."
}

Write-Host 'Building OrcSDR with native ESP-IDF 5.5.4...'
& (Join-Path $PSScriptRoot 'build-tab5-idf.ps1') -IdfPath $IdfPath

Set-Location (Join-Path $PSScriptRoot '..')
Write-Host "Flashing OrcSDR to $Port. Saved settings and Wi-Fi profiles are preserved."
idf.py -B build-native-hosted3 -p $Port flash
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host 'Install complete. Connect the RTL-SDR to the Tab5 USB Host port and reset the Tab5.'
