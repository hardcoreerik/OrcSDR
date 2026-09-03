#requires -Version 5.1
<#
.SYNOPSIS
Builds the Tab5 P4 image and emits the M5Burner upload bundle for one tag.

.DESCRIPTION
This never flashes a device and never touches NVS. It makes two ordered Tab5
packages: a temporary P4 C6 bridge, then the final OrcSDR P4 application.
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
if ($Version -notmatch '^v\d+\.\d+\.\d+(-(alpha|beta)\.\d+)?(-candidate\.\d+)?$') {
  throw "Use an exact OrcSDR release tag or candidate label, not '$Version'."
}
$exactTag = (git describe --tags --exact-match 2>$null)
if ($LASTEXITCODE) { $exactTag = '' } else { $exactTag = $exactTag.Trim() }
if ($exactTag -ne $Version) { throw "Build only from exact tag $Version (HEAD is '$exactTag')." }
if ((git status --porcelain --untracked-files=no)) {
  throw 'Tracked source changes are present; package only a clean tagged tree.'
}

$app = Join-Path $repo 'apps\orcsdr-tab5'
$build = 'build-native-hosted3'
$appBuild = Join-Path $app $build
if (-not $SkipBuild) {
  & (Join-Path $app 'tools\build-tab5-idf.ps1') -IdfPath $IdfPath
  if ($LASTEXITCODE -ne 0) { throw "Native build failed ($LASTEXITCODE)." }
}

$binary = Join-Path $app "$build\merged-binary.bin"
. (Join-Path $IdfPath 'export.ps1')
Push-Location $app
try {
  idf.py -B $build merge-bin --format raw --output $binary
  if ($LASTEXITCODE -ne 0) { throw "merge-bin failed ($LASTEXITCODE)." }
} finally { Pop-Location }
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
  c6_requirement = 'Install OrcSDR Hosted 3.0.6 Bridge through M5Burner before this final package.'
  erase_policy = 'Do not erase for normal upgrades.'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dist 'm5burner-upload.json')
@(
  "OrcSDR for M5Stack Tab5 $Version",
  '',
  'This package flashes the ESP32-P4 application only.',
  'Before first use, install OrcSDR Hosted 3.0.6 Bridge through M5Burner, then install this final package.',
  'Normal upgrades: do not erase; this preserves OrcSDR settings and saved Wi-Fi profiles.',
  "SHA-256: $hash"
) | Set-Content -LiteralPath (Join-Path $dist 'README.txt')
Copy-Item -LiteralPath (Join-Path $repo 'docs\images\OrcSDR-Main.png') `
  -Destination (Join-Path $dist 'OrcSDR-Main.png') -Force

$localRoot = Join-Path $dist 'local-m5burner'
$localFirmware = Join-Path $localRoot 'firmware'
New-Item -ItemType Directory -Force -Path $localFirmware | Out-Null
Copy-Item -LiteralPath (Join-Path $appBuild 'bootloader\bootloader.bin') `
  -Destination (Join-Path $localFirmware 'bootloader_0x2000.bin') -Force
Copy-Item -LiteralPath (Join-Path $appBuild 'partition_table\partition-table.bin') `
  -Destination (Join-Path $localFirmware 'partition-table_0x8000.bin') -Force
Copy-Item -LiteralPath (Join-Path $appBuild 'orcsdr_tab5.bin') `
  -Destination (Join-Path $localFirmware 'orcsdr_tab5_0x10000.bin') -Force
$localManifest = [ordered]@{
  name = "OrcSDR $Version"
  description = 'OrcSDR P4 application for private Tab5 testing. Do not erase for normal upgrades.'
  keywords = 'Tab5, RTL-SDR, ESP-IDF'
  author = 'hardcoreerik'
  repository = 'https://github.com/hardcoreerik/OrcSDR'
  firmware_category = [ordered]@{
    path = 'firmware'
    device = @('Tab5')
    default_baud = 921600
  }
  version = $Version.TrimStart('v')
  framework = 'ESP-IDF'
}
$localManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $localRoot 'm5burner.json')
@'
#!/bin/bash
esptool.py --chip esp32p4 --port /dev/${port} --baud 921600 --before default_reset --after hard_reset write_flash -z \
--flash_mode dio --flash_freq 80m --flash_size 16MB \
0x2000 bootloader_0x2000.bin \
0x8000 partition-table_0x8000.bin \
0x10000 orcsdr_tab5_0x10000.bin
'@ | Set-Content -LiteralPath (Join-Path $localFirmware 'flash.sh') -NoNewline
$localZip = Join-Path $dist "OrcSDR-Tab5-$Version-local-m5burner.zip"
Compress-Archive -Path (Join-Path $localRoot '*') -DestinationPath $localZip -Force

$c6Dir = Join-Path $dist 'c6'
& (Join-Path $PSScriptRoot 'build-hosted-c6.ps1') -OutputDirectory $c6Dir -IdfPath $IdfPath
if ($LASTEXITCODE) { throw "ESP-Hosted C6 build failed ($LASTEXITCODE)." }
$c6Image = Join-Path $c6Dir 'esp_hosted_tab5_c6.bin'
$bridgeDir = Join-Path $dist 'Hosted-Bridge'
& (Join-Path $PSScriptRoot 'build-c6-bridge.ps1') -C6Firmware $c6Image -OutputDirectory $bridgeDir -IdfPath $IdfPath
if ($LASTEXITCODE) { throw "Hosted bridge build failed ($LASTEXITCODE)." }

$bridgeImage = Join-Path $bridgeDir 'OrcSDR-Hosted-Bridge.bin'
$bridgeHash = (Get-FileHash -LiteralPath $bridgeImage -Algorithm SHA256).Hash.ToLowerInvariant()
$c6Provenance = Get-Content (Join-Path $c6Dir 'c6-provenance.json') -Raw | ConvertFrom-Json
Copy-Item $c6Image (Join-Path $bridgeDir $c6Provenance.firmware) -Force
Copy-Item (Join-Path $c6Dir 'c6-provenance.json') (Join-Path $bridgeDir 'c6-provenance.json') -Force
Set-Content -LiteralPath (Join-Path $bridgeDir 'SHA256SUMS.txt') -NoNewline `
  -Value "$bridgeHash *$(Split-Path $bridgeImage -Leaf)`n"
[ordered]@{
  name = 'OrcSDR Hosted 3.0.6 Bridge'
  version = $Version.TrimStart('v')
  device_type = 'M5Stack Tab5'
  target = 'ESP32-P4'
  temporary = $true
  firmware = (Split-Path $bridgeImage -Leaf)
  sha256 = $bridgeHash
  c6_firmware = $c6Provenance.firmware
  c6_sha256 = $c6Provenance.sha256
  c6_source_revision = $c6Provenance.source_revision
  c6_hosted_version = $c6Provenance.hosted_version
  erase_policy = 'Do not erase. Install this temporary bridge first, then install final OrcSDR.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bridgeDir 'm5burner-upload.json')
@(
  "OrcSDR Hosted 3.0.6 Bridge for M5Stack Tab5 $Version",
  '',
  'Temporary package: it updates the internal C6 through the existing ESP-Hosted SDIO link.',
  'Wait until the bridge logs C6 after OTA: 3.0.6, then install the final OrcSDR package.',
  'Do not erase. A C6 that cannot establish the Hosted link needs manual recovery guidance.',
  "C6 source: $($c6Provenance.source_revision)",
  "C6 SHA-256: $($c6Provenance.sha256)",
  "Bridge SHA-256: $bridgeHash"
) | Set-Content -LiteralPath (Join-Path $bridgeDir 'README.txt')
Copy-Item -LiteralPath (Join-Path $repo 'docs\images\OrcSDR-Main.png') -Destination (Join-Path $bridgeDir 'OrcSDR-Main.png') -Force
$bridgeLocal = Join-Path $bridgeDir 'local-m5burner\firmware'
New-Item -ItemType Directory -Force -Path $bridgeLocal | Out-Null
Copy-Item (Join-Path $bridgeDir 'bootloader_0x2000.bin') (Join-Path $bridgeLocal 'bootloader_0x2000.bin') -Force
Copy-Item (Join-Path $bridgeDir 'partition-table_0x8000.bin') (Join-Path $bridgeLocal 'partition-table_0x8000.bin') -Force
Copy-Item (Join-Path $bridgeDir 'orcsdr_c6_bridge.bin') (Join-Path $bridgeLocal 'orcsdr_c6_bridge_0x10000.bin') -Force
[ordered]@{
  name = "OrcSDR Hosted 3.0.6 Bridge $Version"
  description = 'Temporary Tab5 P4 bridge. Install first; it updates the internal C6 without erasing NVS.'
  keywords = 'Tab5, ESP-Hosted, C6, bridge'
  author = 'hardcoreerik'
  repository = 'https://github.com/hardcoreerik/OrcSDR'
  firmware_category = [ordered]@{ path = 'firmware'; device = @('Tab5'); default_baud = 921600 }
  version = $Version.TrimStart('v')
  framework = 'ESP-IDF'
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $bridgeDir 'local-m5burner\m5burner.json')
Compress-Archive -Path (Join-Path $bridgeDir 'local-m5burner\*') -DestinationPath (Join-Path $bridgeDir "OrcSDR-Hosted-Bridge-$Version-local-m5burner.zip") -Force
Write-Host "M5Burner bundle ready: $dist"
