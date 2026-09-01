#requires -Version 5.1
<#
.SYNOPSIS
Validates an already-built OrcSDR M5Burner upload bundle without flashing.
#>
param(
  [Parameter(Mandatory)]
  [string]$BundlePath,
  [string]$Version
)

$ErrorActionPreference = 'Stop'
$bundle = (Resolve-Path -LiteralPath $BundlePath).Path
$manifestPath = Join-Path $bundle 'm5burner-upload.json'
$sumPath = Join-Path $bundle 'SHA256SUMS.txt'
$coverPath = Join-Path $bundle 'OrcSDR-Main.png'
$readmePath = Join-Path $bundle 'README.txt'
foreach ($path in @($manifestPath, $sumPath, $coverPath, $readmePath)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing bundle file: $path" }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (-not $Version) { $Version = "v$($manifest.version)" }
if ($Version -notmatch '^v\d+\.\d+\.\d+(-alpha\.\d+)?(-candidate\.\d+)?$') { throw "Invalid version: $Version" }
if ($manifest.name -ne 'OrcSDR' -or $manifest.version -ne $Version.TrimStart('v')) {
  throw 'Manifest name or version does not match the expected release.'
}
if ($manifest.device_type -ne 'M5Stack Tab5' -or $manifest.target -ne 'ESP32-P4') {
  throw 'Manifest is not a Tab5 P4 release.'
}

$imagePath = Join-Path $bundle $manifest.firmware
if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) { throw "Missing firmware: $imagePath" }
$sumLine = (Get-Content -LiteralPath $sumPath -Raw).Trim()
if ($sumLine -notmatch '^([0-9a-fA-F]{64}) \*(.+)$') { throw 'SHA256SUMS.txt must contain one SHA-256 entry.' }
if ($Matches[2] -ne $manifest.firmware) { throw 'SHA256SUMS filename does not match the manifest.' }
$actualHash = (Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($Matches[1].ToLowerInvariant() -ne $actualHash -or $manifest.sha256 -ne $actualHash) {
  throw 'Firmware SHA-256 does not match the manifest and checksum file.'
}
if ((Get-Content -LiteralPath $readmePath -Raw) -notmatch 'P4 application only') {
  throw 'Bundle README is missing the P4-only safety notice.'
}

Write-Host "M5BURNER_BUNDLE_OK version=$Version sha256=$actualHash"
