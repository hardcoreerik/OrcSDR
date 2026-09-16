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

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  $stream = [IO.File]::OpenRead($Path)
  try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
  finally { $stream.Dispose(); $sha.Dispose() }
}

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
$releaseNotesPath = Join-Path $bundle 'RELEASE_NOTES.txt'
$requiredPaths = @($manifestPath, $sumPath, $coverPath, $readmePath)
if (-not $Bridge) { $requiredPaths += $releaseNotesPath }
foreach ($path in $requiredPaths) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing bundle file: $path" }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (-not $Version) { $Version = "v$($manifest.version)" }
if ($Version -notmatch '^v\d+\.\d+\.\d+(-(alpha|beta|rc)\.?\d+)?(-(candidate\.\d+|multidongle-rc\d+))?$') { throw "Invalid version: $Version" }
if ($manifest.version -ne $Version.TrimStart('v')) { throw 'Manifest version does not match the expected release.' }
if ($Bridge) {
  if ($manifest.name -ne 'OrcSDR Hosted 3.0.6 Bridge' -or -not $manifest.temporary) { throw 'Manifest is not the temporary Hosted bridge.' }
  $provenancePath = Join-Path $bundle 'c6-provenance.json'
  if (-not (Test-Path $provenancePath)) { throw 'Bridge is missing C6 provenance.' }
  $provenance = Get-Content $provenancePath -Raw | ConvertFrom-Json
  $c6Image = Join-Path $bundle $provenance.firmware
  if ($provenance.hosted_version -ne '3.0.6' -or -not (Test-Path $c6Image)) { throw 'Bridge C6 image/version is invalid.' }
  if ((Get-Sha256 $c6Image) -ne $provenance.sha256) { throw 'Bridge C6 hash does not match provenance.' }
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
      $manifest.c6_source_revision -ne $provenance.source_revision -or
      (Get-Sha256 $c6Image) -ne $provenance.sha256) {
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
  try { $embeddedHash = [BitConverter]::ToString($sha.ComputeHash($embedded)).Replace('-', '').ToLowerInvariant() }
  finally { $sha.Dispose() }
  if ($embeddedHash -ne $provenance.sha256) { throw 'Embedded C6 hash does not match provenance.' }
}
$firmwareName = [regex]::Escape($manifest.firmware)
$sumLine = @(Get-Content -LiteralPath $sumPath |
  Where-Object { $_ -match "^[0-9a-fA-F]{64} \*$firmwareName$" })
if ($sumLine.Count -ne 1 -or $sumLine[0] -notmatch '^([0-9a-fA-F]{64}) \*(.+)$') {
  throw 'SHA256SUMS.txt must contain exactly one entry for the declared firmware.'
}
$actualHash = Get-Sha256 $imagePath
if ($Matches[1].ToLowerInvariant() -ne $actualHash -or $manifest.sha256 -ne $actualHash) {
  throw 'Firmware SHA-256 does not match the manifest and checksum file.'
}
if (-not $Bridge -and (Get-Content -LiteralPath $readmePath -Raw) -notmatch 'Firmware & Updates') {
  throw 'Bundle README is missing the in-app C6 update guidance.'
}
if (-not $Bridge -and [string]::IsNullOrWhiteSpace((Get-Content -LiteralPath $releaseNotesPath -Raw))) {
  throw 'Bundle release notes are empty.'
}

$zip = Get-ChildItem -LiteralPath $bundle -Filter '*local-m5burner.zip' | Select-Object -First 1
if (-not $zip) { throw 'Missing local M5Burner package zip.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip.FullName)
try {
  $names = @($archive.Entries | ForEach-Object FullName)
  foreach ($entry in @('m5burner.json', 'firmware/bootloader_0x2000.bin', 'firmware/partition-table_0x8000.bin', 'firmware/flash.sh')) {
    if ($names -notcontains $entry) { throw "M5Burner zip missing $entry" }
  }
  $flashEntry = $archive.Entries | Where-Object FullName -eq 'firmware/flash.sh'
  $flashReader = [IO.BinaryReader]::new($flashEntry.Open())
  try { $flashBytes = $flashReader.ReadBytes([int]$flashEntry.Length) }
  finally { $flashReader.Dispose() }
  $utf8 = [Text.UTF8Encoding]::new($false, $true)
  try { $flashText = $utf8.GetString($flashBytes) }
  catch [Text.DecoderFallbackException] {
    throw 'M5Burner ZIP flash.sh must use valid UTF-8 without BOM and Linux LF line endings.'
  }
  if (($flashBytes.Length -ge 3 -and $flashBytes[0] -eq 0xef -and $flashBytes[1] -eq 0xbb -and $flashBytes[2] -eq 0xbf) -or
      $flashBytes -contains [byte]13 -or
      -not $flashText.StartsWith("#!/bin/bash`n")) {
    throw 'M5Burner ZIP flash.sh must use UTF-8 without BOM and Linux LF line endings.'
  }
  $appEntry = if ($Bridge) { 'firmware/orcsdr_c6_bridge_0x10000.bin' } else { 'firmware/orcsdr_tab5_0x10000.bin' }
  if ($names -notcontains $appEntry) { throw "M5Burner zip missing $appEntry" }
  if (-not $Bridge) {
    if ($names -notcontains 'c6-provenance.json') { throw 'M5Burner zip is missing C6 provenance.' }
    if ($names -notcontains 'RELEASE_NOTES.txt') { throw 'M5Burner zip is missing release notes.' }
    $provenanceReader = [IO.StreamReader]::new(($archive.Entries | Where-Object FullName -eq 'c6-provenance.json').Open())
    try { $zipProvenance = $provenanceReader.ReadToEnd() | ConvertFrom-Json }
    finally { $provenanceReader.Dispose() }
    if ($zipProvenance.hosted_version -ne $provenance.hosted_version -or
        $zipProvenance.sha256 -ne $provenance.sha256 -or
        $zipProvenance.source_revision -ne $provenance.source_revision) {
      throw 'M5Burner ZIP C6 provenance does not match the release provenance.'
    }
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
