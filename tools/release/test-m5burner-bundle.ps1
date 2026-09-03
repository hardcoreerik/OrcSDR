#requires -Version 5.1
<#
.SYNOPSIS
Validates an already-built OrcSDR M5Burner upload bundle without flashing.
#>
param(
  [Parameter(Mandatory)]
  [string]$BundlePath,
  [string]$Version,
  [switch]$Bridge
)

$ErrorActionPreference = 'Stop'

function Find-ByteSequence([byte[]]$Haystack, [byte[]]$Needle) {
  if ($Needle.Length -eq 0 -or $Needle.Length -gt $Haystack.Length) { return -1 }
  $last = $Haystack.Length - $Needle.Length
  for ($offset = 0; $offset -le $last; $offset++) {
    if ($Haystack[$offset] -ne $Needle[0]) { continue }
    $matched = $true
    for ($index = 1; $index -lt $Needle.Length; $index++) {
      if ($Haystack[$offset + $index] -ne $Needle[$index]) { $matched = $false; break }
    }
    if ($matched) { return $offset }
  }
  return -1
}

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
if ($Version -notmatch '^v\d+\.\d+\.\d+(-(alpha|beta)\.\d+)?(-candidate\.\d+)?$') { throw "Invalid version: $Version" }
if ($manifest.version -ne $Version.TrimStart('v')) { throw 'Manifest version does not match the expected release.' }
if ($Bridge) {
  if ($manifest.name -ne 'OrcSDR Hosted 3.0.6 Bridge' -or -not $manifest.temporary) { throw 'Manifest is not the temporary Hosted bridge.' }
  $provenancePath = Join-Path $bundle 'c6-provenance.json'
  if (-not (Test-Path $provenancePath)) { throw 'Bridge is missing C6 provenance.' }
  $provenance = Get-Content $provenancePath -Raw | ConvertFrom-Json
  $c6Image = Join-Path $bundle $provenance.firmware
  if ($provenance.hosted_version -ne '3.0.6' -or -not (Test-Path $c6Image)) { throw 'Bridge C6 image/version is invalid.' }
  if ((Get-FileHash $c6Image -Algorithm SHA256).Hash.ToLowerInvariant() -ne $provenance.sha256) { throw 'Bridge C6 hash does not match provenance.' }
} elseif ($manifest.name -ne 'OrcSDR') {
  throw 'Manifest is not the final OrcSDR package.'
} else {
  $provenancePath = Join-Path $bundle 'c6\c6-provenance.json'
  $c6Image = Join-Path $bundle $manifest.c6_firmware
  if (-not $manifest.c6_firmware -or -not (Test-Path $provenancePath) -or -not (Test-Path $c6Image)) {
    throw 'Final package is missing its embedded C6 update provenance.'
  }
  $provenance = Get-Content $provenancePath -Raw | ConvertFrom-Json
  if ($provenance.hosted_version -ne '3.0.6' -or $manifest.c6_sha256 -ne $provenance.sha256 -or
      (Get-FileHash $c6Image -Algorithm SHA256).Hash.ToLowerInvariant() -ne $provenance.sha256) {
    throw 'Final package C6 image does not match its provenance.'
  }
}
if ($manifest.device_type -ne 'M5Stack Tab5' -or $manifest.target -ne 'ESP32-P4') {
  throw 'Manifest is not a Tab5 P4 release.'
}

$imagePath = Join-Path $bundle $manifest.firmware
if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) { throw "Missing firmware: $imagePath" }
if (-not $Bridge -and (Get-Item -LiteralPath $imagePath).Length -le (Get-Item -LiteralPath $c6Image).Length) {
  throw 'Final P4 image is too small to contain the declared C6 update image.'
}
if (-not $Bridge) {
  $imageBytes = [IO.File]::ReadAllBytes($imagePath)
  $c6Bytes = [IO.File]::ReadAllBytes($c6Image)
  $embeddedOffset = Find-ByteSequence $imageBytes $c6Bytes
  if ($embeddedOffset -lt 0) { throw 'Final P4 image does not contain the declared C6 image.' }
  $embedded = [byte[]]::new($c6Bytes.Length)
  [Array]::Copy($imageBytes, $embeddedOffset, $embedded, 0, $embedded.Length)
  $sha = [Security.Cryptography.SHA256]::Create()
  try { $embeddedHash = [Convert]::ToHexString($sha.ComputeHash($embedded)).ToLowerInvariant() }
  finally { $sha.Dispose() }
  if ($embeddedHash -ne $provenance.sha256) { throw 'Embedded C6 hash does not match provenance.' }
}
$sumLine = (Get-Content -LiteralPath $sumPath -Raw).Trim()
if ($sumLine -notmatch '^([0-9a-fA-F]{64}) \*(.+)$') { throw 'SHA256SUMS.txt must contain one SHA-256 entry.' }
if ($Matches[2] -ne $manifest.firmware) { throw 'SHA256SUMS filename does not match the manifest.' }
$actualHash = (Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($Matches[1].ToLowerInvariant() -ne $actualHash -or $manifest.sha256 -ne $actualHash) {
  throw 'Firmware SHA-256 does not match the manifest and checksum file.'
}
if (-not $Bridge -and (Get-Content -LiteralPath $readmePath -Raw) -notmatch 'Firmware & Updates') {
  throw 'Bundle README is missing the in-app C6 update guidance.'
}

$zip = Get-ChildItem -LiteralPath $bundle -Filter '*local-m5burner.zip' | Select-Object -First 1
if (-not $zip) { throw 'Missing local M5Burner package zip.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip.FullName)
try {
  $names = @($archive.Entries | ForEach-Object FullName)
  foreach ($entry in @('m5burner.json', 'firmware/bootloader_0x2000.bin', 'firmware/partition-table_0x8000.bin')) {
    if ($names -notcontains $entry) { throw "M5Burner zip missing $entry" }
  }
  $appEntry = if ($Bridge) { 'firmware/orcsdr_c6_bridge_0x10000.bin' } else { 'firmware/orcsdr_tab5_0x10000.bin' }
  if ($names -notcontains $appEntry) { throw "M5Burner zip missing $appEntry" }
  if (-not $Bridge) {
    if ($names -notcontains 'c6-provenance.json') { throw 'M5Burner zip is missing C6 provenance.' }
    $reader = [IO.StreamReader]::new(($archive.Entries | Where-Object FullName -eq 'm5burner.json').Open())
    try { $zipManifest = $reader.ReadToEnd() | ConvertFrom-Json }
    finally { $reader.Dispose() }
    if ($zipManifest.embedded_c6.hosted_version -ne '3.0.6' -or
        $zipManifest.embedded_c6.sha256 -ne $provenance.sha256 -or
        $zipManifest.embedded_c6.source_revision -ne $provenance.source_revision) {
      throw 'M5Burner ZIP embedded-C6 metadata does not match provenance.'
    }
  }
} finally { $archive.Dispose() }

Write-Host "M5BURNER_BUNDLE_OK type=$(if($Bridge){'bridge'}else{'final'}) version=$Version sha256=$actualHash"
