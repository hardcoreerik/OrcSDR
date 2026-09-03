#requires -Version 5.1
<#
.SYNOPSIS
Builds the Tab5 P4 image and emits the M5Burner upload bundle for one tag.

.DESCRIPTION
This never flashes a device and never touches NVS. It makes one Tab5 P4
package with the pinned C6 image embedded for an explicit in-app update.
#>
param(
  [string]$Version,
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4',
  [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  $stream = [IO.File]::OpenRead($Path)
  try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
  finally { $stream.Dispose(); $sha.Dispose() }
}

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
if ($SkipBuild) {
  throw 'SkipBuild is not supported: every release package must rebuild the P4 with its exact embedded C6 image.'
}

$app = Join-Path $repo 'apps\orcsdr-tab5'
$build = 'build-native-hosted3'
$appBuild = Join-Path $app $build
$dist = Join-Path $repo "dist\OrcSDR-Tab5-$Version"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$c6Dir = Join-Path $dist 'c6'
& (Join-Path $PSScriptRoot 'build-hosted-c6.ps1') -OutputDirectory $c6Dir -IdfPath $IdfPath
if ($LASTEXITCODE) { throw "ESP-Hosted C6 build failed ($LASTEXITCODE)." }
$c6Image = Join-Path $c6Dir 'esp_hosted_tab5_c6.bin'
$c6Provenance = Get-Content (Join-Path $c6Dir 'c6-provenance.json') -Raw | ConvertFrom-Json
& (Join-Path $app 'tools\build-tab5-idf.ps1') -IdfPath $IdfPath -C6Firmware $c6Image
if ($LASTEXITCODE -ne 0) { throw "Native build failed ($LASTEXITCODE)." }
$appImage = Join-Path $appBuild 'orcsdr_tab5.bin'
if (-not (Test-Path -LiteralPath $appImage)) { throw "Missing built P4 image: $appImage" }
$appPartitionBytes = 0x400000
$releaseReserveBytes = 0x40000
if ((Get-Item -LiteralPath $appImage).Length -gt ($appPartitionBytes - $releaseReserveBytes)) {
  throw 'P4 image leaves less than 256 KiB in the app partition; refuse the embedded-C6 release bundle.'
}

$binary = Join-Path $app "$build\merged-binary.bin"
. (Join-Path $IdfPath 'export.ps1')
Push-Location $app
try {
  idf.py -B $build merge-bin --format raw --output $binary
  if ($LASTEXITCODE -ne 0) { throw "merge-bin failed ($LASTEXITCODE)." }
} finally { Pop-Location }
if (-not (Test-Path -LiteralPath $binary)) { throw "Missing merged P4 image: $binary" }

$image = Join-Path $dist "OrcSDR-Tab5-$Version.bin"
Copy-Item -LiteralPath $binary -Destination $image -Force
$hash = Get-Sha256 $image
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
  c6_requirement = 'Embedded C6 3.0.6 image; update only from Firmware & Updates after confirmation.'
  c6_firmware = "c6/$($c6Provenance.firmware)"
  c6_sha256 = $c6Provenance.sha256
  c6_source_revision = $c6Provenance.source_revision
  erase_policy = 'Do not erase for normal upgrades.'
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dist 'm5burner-upload.json')
@(
  "OrcSDR for M5Stack Tab5 $Version",
  '',
  'This package contains the P4 application and a pinned C6 Hosted 3.0.6 update image.',
  'If the reachable C6 version differs, open Settings > Firmware & Updates and confirm the in-app update.',
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
Copy-Item -LiteralPath (Join-Path $c6Dir 'c6-provenance.json') `
  -Destination (Join-Path $localRoot 'c6-provenance.json') -Force
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
  embedded_c6 = [ordered]@{
    hosted_version = $c6Provenance.hosted_version
    sha256 = $c6Provenance.sha256
    source_revision = $c6Provenance.source_revision
  }
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

Write-Host "M5Burner bundle ready: $dist"
