#requires -Version 5.1
<#
.SYNOPSIS
Builds the Tab5 P4 image and emits the M5Burner upload bundle for one tag.

.DESCRIPTION
This never flashes a device and never touches NVS. The Tab5 C6 ESP-Hosted
firmware is deliberately excluded: M5Burner must update it separately.
#>
param(
  [string]$Version,
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4',
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $repo
if (-not $Version) { $Version = (git describe --tags --exact-match).Trim() }
if ($Version -notmatch '^v\d+\.\d+\.\d+(-alpha\.\d+)?$') {
  throw "Use an exact semantic OrcSDR tag, not '$Version'."
}
if ((git status --porcelain --untracked-files=no)) {
  throw 'Tracked source changes are present; package only a clean tagged tree.'
}

$app = Join-Path $repo 'apps\orcsdr-tab5'
$build = 'build-native-hosted3'
if (-not $SkipBuild) {
  & (Join-Path $app 'tools\build-tab5-idf.ps1') -IdfPath $IdfPath
  if ($LASTEXITCODE -ne 0) { throw "Native build failed ($LASTEXITCODE)." }
}

$binary = Join-Path $app "$build\merged-binary.bin"
if (-not (Test-Path -LiteralPath $binary)) {
  . (Join-Path $IdfPath 'export.ps1')
  Push-Location $app
  try {
    idf.py -B $build merge-bin --format raw --output $binary
    if ($LASTEXITCODE -ne 0) { throw "merge-bin failed ($LASTEXITCODE)." }
  } finally { Pop-Location }
}
if (-not (Test-Path -LiteralPath $binary)) { throw "Missing merged P4 image: $binary" }

$dist = Join-Path $repo "dist\OrcSDR-Tab5-$Version"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$image = Join-Path $dist "OrcSDR-Tab5-$Version.bin"
Copy-Item -LiteralPath $binary -Destination $image -Force
$hash = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -NoNewline `
  -Value "$hash *$(Split-Path $image -Leaf)`n"

$manifest = [ordered]@{
  name = 'OrcSDR'
  version = $Version.TrimStart('v')
  device_type = 'M5Stack Tab5'
  target = 'ESP32-P4'
  framework = 'ESP-IDF 5.5.4'
  github = 'https://github.com/hardcoreerik/OrcSDR'
  firmware = (Split-Path $image -Leaf)
  sha256 = $hash
  required_accessory = 'RTL-SDR Blog V4'
  c6_requirement = 'ESP-Hosted 3.0.6 installed separately through M5Burner'
  erase_policy = 'Do not erase for normal upgrades.'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dist 'm5burner-upload.json')
@(
  "OrcSDR for M5Stack Tab5 $Version",
  '',
  'This package flashes the ESP32-P4 application only.',
  'Before first use, install matching ESP-Hosted 3.0.6 on the internal ESP32-C6 through M5Burner.',
  'Normal upgrades: do not erase; this preserves OrcSDR settings and saved Wi-Fi profiles.',
  "SHA-256: $hash"
) | Set-Content -LiteralPath (Join-Path $dist 'README.txt')
Copy-Item -LiteralPath (Join-Path $repo 'docs\images\OrcSDR-Main.png') `
  -Destination (Join-Path $dist 'OrcSDR-Main.png') -Force
Write-Host "M5Burner bundle ready: $dist"
