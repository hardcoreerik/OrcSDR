#requires -Version 5.1
<# Builds the release-owned ESP-Hosted C6 image; it never flashes hardware. #>
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

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lock = Get-Content (Join-Path $PSScriptRoot 'hosted-c6-release.json') -Raw | ConvertFrom-Json
$project = Join-Path $repo $lock.source_project
$localSources = @('CMakeLists.txt', 'main\CMakeLists.txt', 'main\main.c',
                  'sdkconfig.defaults', 'partitions_eh_cp_ota_4m.csv')
$localHashes = ($localSources | ForEach-Object {
  "$_=$((Get-Sha256 (Join-Path $project $_)))"
}) -join "`n"
$localSha = [Security.Cryptography.SHA256]::Create()
try {
  $localSourceSha256 = [BitConverter]::ToString(
    $localSha.ComputeHash([Text.Encoding]::UTF8.GetBytes($localHashes))).Replace('-', '').ToLowerInvariant()
} finally { $localSha.Dispose() }
if (-not (Test-Path (Join-Path $IdfPath 'export.ps1'))) { throw "ESP-IDF 5.5.4 is required at $IdfPath." }

if (-not (Test-Path (Join-Path $SourceDirectory '.git'))) {
  git clone $lock.source_repository $SourceDirectory
  if ($LASTEXITCODE) { throw 'Could not clone the pinned Espressif ESP-Hosted source.' }
}
if ((git -C $SourceDirectory status --porcelain --untracked-files=no) -ne $null) {
  throw 'Pinned ESP-Hosted source has tracked changes; refusing to overwrite them.'
}
if ((git -C $SourceDirectory rev-parse HEAD).Trim() -ne $lock.source_revision) {
  git -C $SourceDirectory fetch origin $lock.source_revision
  if ($LASTEXITCODE) { throw 'Could not fetch the pinned ESP-Hosted revision.' }
  git -C $SourceDirectory checkout --detach $lock.source_revision
  if ($LASTEXITCODE) { throw 'Could not check out the pinned ESP-Hosted revision.' }
}
if ((git -C $SourceDirectory rev-parse HEAD).Trim() -ne $lock.source_revision) { throw 'ESP-Hosted revision mismatch.' }
git -C $SourceDirectory submodule update --init --recursive
if ($LASTEXITCODE) { throw 'Could not initialize the pinned ESP-Hosted submodules.' }

$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$build = 'build'
$componentRoot = Join-Path $SourceDirectory '.orcsdr-components'
$componentLink = Join-Path $componentRoot 'esp_hosted'
if (Test-Path $componentLink) {
  $item = Get-Item -LiteralPath $componentLink
  if (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
      [IO.Path]::GetFullPath($item.Target) -ne [IO.Path]::GetFullPath($SourceDirectory)) {
    throw "Refusing to replace component path: $componentLink"
  }
} else {
  New-Item -ItemType Directory -Force -Path $componentRoot | Out-Null
  New-Item -ItemType Junction -Path $componentLink -Target $SourceDirectory | Out-Null
}
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
$env:PATH = "C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. (Join-Path $IdfPath 'export.ps1')
Push-Location $project
try {
  idf.py -B $build -D "EXTRA_COMPONENT_DIRS=$componentRoot" set-target esp32c6
  if ($LASTEXITCODE) { throw 'ESP-Hosted C6 target configuration failed.' }
  idf.py -B $build -D "EXTRA_COMPONENT_DIRS=$componentRoot" build
  if ($LASTEXITCODE) { throw 'ESP-Hosted C6 build failed.' }
  $sourceImage = Join-Path $project "$build\orcdial_hosted_c6.bin"
  if (-not (Test-Path $sourceImage)) { throw "Missing C6 application image: $sourceImage" }
  New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
  $image = Join-Path $OutputDirectory $lock.output_name
  Copy-Item $sourceImage $image -Force
} finally { Pop-Location }

$hash = Get-Sha256 $image
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
  toolchain = (& riscv32-esp-elf-gcc --version | Select-Object -First 1)
  sdkconfig_sha256 = Get-Sha256 (Join-Path $project 'sdkconfig')
  firmware = (Split-Path $image -Leaf)
  bytes = (Get-Item $image).Length
  sha256 = $hash
} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'c6-provenance.json')
Write-Host "HOSTED_C6_BUILD_OK version=$($lock.hosted_version) bytes=$((Get-Item $image).Length) sha256=$hash"
