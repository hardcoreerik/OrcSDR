param(
  [Parameter(Mandatory = $true)]
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port,
  [string]$IdfId = 'esp-idf-5.5.3'
)

$ErrorActionPreference = 'Stop'
$initializer = 'C:\Espressif\Initialize-Idf.ps1'
if (-not (Test-Path -LiteralPath $initializer)) {
  throw 'ESP-IDF is not installed. Install Espressif IDF 5.5.3, then run this command again.'
}

Write-Host 'Building OrcSDR with native ESP-IDF...'
. $initializer -IdfId $IdfId
& (Join-Path $PSScriptRoot 'build-tab5-idf.ps1') -IdfId $IdfId
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Set-Location (Join-Path $PSScriptRoot '..')
Write-Host "Flashing OrcSDR to $Port. Saved settings and Wi-Fi profiles are preserved."
idf.py -p $Port flash
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host 'Install complete. Connect the RTL-SDR to the Tab5 USB Host port and reset the Tab5.'
