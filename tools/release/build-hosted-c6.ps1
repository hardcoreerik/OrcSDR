#requires -Version 5.1
<# Builds pinned ESP-Hosted C6 firmware and its source-built ROM-SDIO recovery segments. Never flashes hardware. #>
param(
  [Parameter(Mandatory)] [string]$OutputDirectory,
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4',
  [string]$SourceDirectory = (Join-Path $env:TEMP 'OrcSDR-esp-hosted-3.0.6')
)

$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  $stream = [IO.File]::OpenRead($Path)
  try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
  finally { $stream.Dispose(); $sha.Dispose() }
}

function Convert-FlashSize([string]$Value) {
  if ($Value -match '^([0-9]+)(B|KB|MB|GB)$') {
    $n = [UInt64]::Parse($Matches[1])
    switch ($Matches[2]) {
      'B' { return $n }
      'KB' { return $n * 1KB }
      'MB' { return $n * 1MB }
      'GB' { return $n * 1GB }
    }
  }
  throw "Unsupported generated flash size '$Value'."
}

function Convert-Offset([string]$Value) {
  if ($Value -notmatch '^0x[0-9a-fA-F]+$') { throw "Invalid generated flash offset '$Value'." }
  return [UInt64]::Parse($Value.Substring(2), [Globalization.NumberStyles]::HexNumber)
}

function Convert-PartitionNumber([string]$Value) {
  if ($Value -match '^0x[0-9a-fA-F]+$') {
    return [UInt64]::Parse($Value.Substring(2), [Globalization.NumberStyles]::HexNumber)
  }
  return [UInt64]::Parse($Value)
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lock = Get-Content (Join-Path $PSScriptRoot 'hosted-c6-release.json') -Raw | ConvertFrom-Json
$project = Join-Path $repo $lock.source_project
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$localSources = @('CMakeLists.txt', 'main\CMakeLists.txt', 'main\main.c',
                  'sdkconfig.defaults', 'partitions_eh_cp_ota_4m.csv')
$localHashes = ($localSources | ForEach-Object {
  "$($_)=$((Get-Sha256 (Join-Path $project $_)))"
}) -join "`n"
$localSha = [Security.Cryptography.SHA256]::Create()
try {
  $localSourceSha256 = [BitConverter]::ToString(
    $localSha.ComputeHash([Text.Encoding]::UTF8.GetBytes($localHashes))).Replace('-', '').ToLowerInvariant()
} finally { $localSha.Dispose() }
if (-not (Test-Path (Join-Path $IdfPath 'export.ps1'))) { throw "ESP-IDF 5.5.4 is required at $IdfPath." }

# Existing source directories are never fetched, checked out, cleaned, or reset.
function Test-PinnedCleanSource([string]$Path) {
  if (-not (Test-Path -LiteralPath (Join-Path $Path '.git'))) { return $false }
  $head = (git -C $Path rev-parse HEAD 2>$null).Trim()
  if ($LASTEXITCODE -ne 0 -or $head -ne $lock.source_revision) { return $false }
  $status = git -C $Path status --porcelain --untracked-files=all
  return $LASTEXITCODE -eq 0 -and -not $status
}

# If an existing cache clone is dirty or at another revision, leave it alone and
# create a separate official clone beneath this output directory.
if ((Test-Path -LiteralPath $SourceDirectory) -and -not (Test-PinnedCleanSource $SourceDirectory)) {
  $SourceDirectory = Join-Path $OutputDirectory 'esp-hosted-source'
  if ((Test-Path -LiteralPath $SourceDirectory) -and -not (Test-PinnedCleanSource $SourceDirectory)) {
    throw "Isolated source path exists but is not clean at the pinned revision; leaving it untouched: $SourceDirectory"
  }
}
if (-not (Test-Path -LiteralPath $SourceDirectory)) {
  git clone --no-checkout $lock.source_repository $SourceDirectory
  if ($LASTEXITCODE) { throw 'Could not clone pinned Espressif ESP-Hosted source.' }
  git -C $SourceDirectory checkout --detach $lock.source_revision
  if ($LASTEXITCODE) { throw 'Could not check out the pinned revision in the newly-created source clone.' }
}
if (-not (Test-PinnedCleanSource $SourceDirectory)) {
  throw 'ESP-Hosted source is not clean at the pinned revision; existing files were left untouched.'
}
git -C $SourceDirectory submodule update --init --recursive
if ($LASTEXITCODE) { throw 'Could not initialize the pinned ESP-Hosted submodules.' }
if ((git -C $SourceDirectory status --porcelain --untracked-files=all) -ne $null) {
  throw 'ESP-Hosted source or submodules are not clean after initialization.'
}
foreach ($line in (git -C $SourceDirectory submodule status --recursive)) {
  if ($line -match '^[+\-U]') { throw "ESP-Hosted submodule is not at its locked revision: $line" }
}

$buildProject = Join-Path $OutputDirectory 'project'
New-Item -ItemType Directory -Force -Path (Join-Path $buildProject 'main') | Out-Null
foreach ($relative in $localSources) {
  $destination = Join-Path $buildProject $relative
  New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
  Copy-Item -LiteralPath (Join-Path $project $relative) -Destination $destination -Force
}
$componentRoot = Join-Path $OutputDirectory 'components'
$componentLink = Join-Path $componentRoot 'esp_hosted'
New-Item -ItemType Directory -Force -Path $componentRoot | Out-Null
if (Test-Path -LiteralPath $componentLink) {
  $item = Get-Item -LiteralPath $componentLink
  if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
      [IO.Path]::GetFullPath($item.Target) -ne [IO.Path]::GetFullPath($SourceDirectory)) {
    throw "Refusing to replace component path: $componentLink"
  }
} else {
  New-Item -ItemType Junction -Path $componentLink -Target $SourceDirectory | Out-Null
}

$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
$env:PATH = "C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. (Join-Path $IdfPath 'export.ps1')
Push-Location $buildProject
try {
  idf.py -B build -D "EXTRA_COMPONENT_DIRS=$componentRoot" set-target esp32c6
  if ($LASTEXITCODE) { throw 'ESP-Hosted C6 target configuration failed.' }
  idf.py -B build -D "EXTRA_COMPONENT_DIRS=$componentRoot" build
  if ($LASTEXITCODE) { throw 'ESP-Hosted C6 build failed.' }
} finally { Pop-Location }

$build = Join-Path $buildProject 'build'
$flasherPath = Join-Path $build 'flasher_args.json'
if (-not (Test-Path $flasherPath)) { throw 'Missing generated flasher_args.json; refusing to guess segment offsets.' }
$flasher = Get-Content $flasherPath -Raw | ConvertFrom-Json
if ($flasher.extra_esptool_args.chip -ne 'esp32c6') { throw "Generated target is not ESP32-C6: $($flasher.extra_esptool_args.chip)" }
$flashSizeBytes = Convert-FlashSize $flasher.flash_settings.flash_size

$partitions = @()
foreach ($line in Get-Content (Join-Path $project 'partitions_eh_cp_ota_4m.csv')) {
  $trimmed = $line.Trim()
  if (-not $trimmed -or $trimmed.StartsWith('#')) { continue }
  $fields = @($trimmed.Split(',') | ForEach-Object { $_.Trim() })
  if ($fields.Count -lt 5) { continue }
  $partitions += [pscustomobject]@{
    name=$fields[0]; type=$fields[1]; subtype=$fields[2]
    offset=(Convert-PartitionNumber $fields[3]); size=(Convert-PartitionNumber $fields[4])
  }
}
function Get-Partition([string]$Name) {
  $found = @($partitions | Where-Object name -eq $Name)
  if ($found.Count -ne 1) { throw "Expected exactly one '$Name' partition in the pinned project table." }
  return $found[0]
}
$nvsPartition = Get-Partition 'nvs'
$otaDataPartition = Get-Partition 'otadata'
$phyPartition = Get-Partition 'phy_init'
$ota0Partition = Get-Partition 'ota_0'

$flashEntries = @()
foreach ($property in $flasher.flash_files.PSObject.Properties) {
  $relative = ([string]$property.Value).Replace('\','/')
  $flashEntries += [pscustomobject]@{
    offset=(Convert-Offset $property.Name); path=$relative
    full_path=(Join-Path $build ([string]$property.Value))
  }
}
function Get-FlashEntry([string]$PathSuffix) {
  $found = @($flashEntries | Where-Object { $_.path -eq $PathSuffix })
  if ($found.Count -ne 1) { throw "Generated flash map must contain exactly one '$PathSuffix'." }
  return $found[0]
}
$entries = @(
  [pscustomobject]@{ role='bootloader'; partition='bootloader'; entry=(Get-FlashEntry 'bootloader/bootloader.bin'); file='c6-bootloader.bin' },
  [pscustomobject]@{ role='partition_table'; partition='partition_table'; entry=(Get-FlashEntry 'partition_table/partition-table.bin'); file='c6-partition-table.bin' },
  [pscustomobject]@{ role='ota_data_initial'; partition='otadata'; entry=(Get-FlashEntry 'ota_data_initial.bin'); file='c6-ota_data_initial.bin' },
  [pscustomobject]@{ role='application'; partition=$ota0Partition.name; entry=(Get-FlashEntry 'orcdial_hosted_c6.bin'); file=$lock.output_name }
)

$expectedOffsets = @{
  bootloader=[UInt64]0
  partition_table=[UInt64]0x8000
  ota_data_initial=$otaDataPartition.offset
  application=$ota0Partition.offset
}
$manifestSegments = @()
foreach ($segment in $entries) {
  $entry = $segment.entry
  if ($entry.offset -ne $expectedOffsets[$segment.role]) {
    throw "Generated $($segment.role) offset 0x$('{0:x}' -f $entry.offset) disagrees with the source partition layout."
  }
  if (-not (Test-Path -LiteralPath $entry.full_path -PathType Leaf)) { throw "Missing generated segment: $($entry.full_path)" }
  $bytes = [UInt64](Get-Item -LiteralPath $entry.full_path).Length
  if (-not $bytes -or $entry.offset + $bytes -gt $flashSizeBytes) { throw "Generated $($segment.role) is empty or exceeds C6 flash bounds." }
  if ($segment.role -eq 'application' -and $bytes -gt $ota0Partition.size) { throw 'C6 application does not fit the ota_0 partition.' }
  $destination = Join-Path $OutputDirectory $segment.file
  Copy-Item -LiteralPath $entry.full_path -Destination $destination -Force
  $manifestSegments += [ordered]@{
    role=$segment.role; partition=$segment.partition; file=$segment.file
    offset=('0x{0:x}' -f $entry.offset); size_bytes=$bytes; sha256=(Get-Sha256 $destination)
  }
}

$sorted = @($manifestSegments | Sort-Object { [Convert]::ToUInt64($_.offset.Substring(2),16) })
for ($i=1; $i -lt $sorted.Count; ++$i) {
  $previous = $sorted[$i-1]; $current = $sorted[$i]
  $previousEnd = [Convert]::ToUInt64($previous.offset.Substring(2),16) + [UInt64]$previous.size_bytes
  $currentStart = [Convert]::ToUInt64($current.offset.Substring(2),16)
  if ($previousEnd -gt $currentStart) { throw "Generated flash segments overlap: $($previous.role) and $($current.role)." }
}
foreach ($preserved in @($nvsPartition,$phyPartition)) {
  $start=$preserved.offset; $end=$start+$preserved.size
  foreach ($segment in $manifestSegments) {
    $segmentStart=[Convert]::ToUInt64($segment.offset.Substring(2),16)
    $segmentEnd=$segmentStart+[UInt64]$segment.size_bytes
    if ($segmentStart -lt $end -and $start -lt $segmentEnd) {
      throw "Generated $($segment.role) overlaps preserved $($preserved.name) partition."
    }
  }
}

$hash = Get-Sha256 (Join-Path $OutputDirectory $lock.output_name)
$sdkconfigPath = Join-Path $buildProject 'sdkconfig'
if (-not (Test-Path $sdkconfigPath)) { throw 'Missing generated sdkconfig.' }
$sdkconfigHash=Get-Sha256 $sdkconfigPath
$toolchain = (& riscv32-esp-elf-gcc --version | Select-Object -First 1)
$manifestPath=Join-Path $OutputDirectory 'c6-rom-recovery-manifest.json'
[ordered]@{
  format='orcsdr-c6-rom-recovery-v1'
  trusted=$true
  target=$lock.target
  transport='ESP32-C6 ROM over SDIO'
  hosted_version=$lock.hosted_version
  source_repository=$lock.source_repository
  source_revision=$lock.source_revision
  source_project=$lock.source_project
  local_source_sha256=$localSourceSha256
  idf_version=$lock.idf_version
  toolchain=$toolchain
  sdkconfig_sha256=$sdkconfigHash
  flash_size_bytes=$flashSizeBytes
  segments=$manifestSegments
  preserved_partitions=@(
    [ordered]@{name=$nvsPartition.name;offset=('0x{0:x}' -f $nvsPartition.offset);size_bytes=$nvsPartition.size;write=$false},
    [ordered]@{name=$phyPartition.name;offset=('0x{0:x}' -f $phyPartition.offset);size_bytes=$phyPartition.size;write=$false}
  )
  nvs_migration_note='No NVS image is packaged. Preserve the existing NVS range during ROM writes. The C6 application currently erases NVS when nvs_flash_init reports no free pages or a new NVS version; factory ESP-AT NVS compatibility is not established by this artifact package.'
} | ConvertTo-Json -Depth 8 | Set-Content $manifestPath

[ordered]@{
  hosted_version = $lock.hosted_version
  source_repository = $lock.source_repository
  source_revision = $lock.source_revision
  source_project = $lock.source_project
  local_source_sha256 = $localSourceSha256
  target = $lock.target
  transport = $lock.transport
  board_configuration = 'M5Stack Tab5 internal ESP32-C6; P4 host uses ESP32P4_TAB5_C6_BOARD and qualified 4-bit SDIO at 10 MHz'
  idf_version = $lock.idf_version
  toolchain = $toolchain
  sdkconfig_sha256 = $sdkconfigHash
  firmware = $lock.output_name
  bytes = (Get-Item (Join-Path $OutputDirectory $lock.output_name)).Length
  sha256 = $hash
} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputDirectory 'c6-provenance.json')

Write-Host "HOSTED_C6_BUILD_OK version=$($lock.hosted_version) bytes=$((Get-Item (Join-Path $OutputDirectory $lock.output_name)).Length) sha256=$hash"
Write-Host "C6_ROM_MANIFEST_OK target=$($lock.target) segments=$($manifestSegments.Count) flash_bytes=$flashSizeBytes manifest=$manifestPath"
