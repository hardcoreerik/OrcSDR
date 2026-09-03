#requires -Version 5.1
<# Builds the release-owned ESP-Hosted C6 image; it never flashes hardware. #>
param(
  [Parameter(Mandatory)] [string]$OutputDirectory,
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4',
  [string]$SourceDirectory = (Join-Path $env:TEMP 'OrcSDR-esp-hosted-3.0.6')
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lock = Get-Content (Join-Path $PSScriptRoot 'hosted-c6-release.json') -Raw | ConvertFrom-Json
if (-not (Test-Path (Join-Path $IdfPath 'export.ps1'))) { throw "ESP-IDF 5.5.4 is required at $IdfPath." }

if (-not (Test-Path (Join-Path $SourceDirectory '.git'))) {
  git clone $lock.source_repository $SourceDirectory
  if ($LASTEXITCODE) { throw 'Could not clone the pinned Espressif ESP-Hosted source.' }
}
git -C $SourceDirectory fetch origin $lock.source_revision
if ($LASTEXITCODE) { throw 'Could not fetch the pinned ESP-Hosted revision.' }
git -C $SourceDirectory checkout --detach $lock.source_revision
if ($LASTEXITCODE) { throw 'Could not check out the pinned ESP-Hosted revision.' }
if ((git -C $SourceDirectory rev-parse HEAD).Trim() -ne $lock.source_revision) { throw 'ESP-Hosted revision mismatch.' }
git -C $SourceDirectory submodule update --init --recursive
if ($LASTEXITCODE) { throw 'Could not initialize the pinned ESP-Hosted submodules.' }

$project = Join-Path $SourceDirectory $lock.source_project
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$build = 'build-orcsdr-tab5-c6'
$componentRoot = Join-Path $SourceDirectory '.orcsdr-components'
$componentLink = Join-Path $componentRoot 'esp_hosted'
if (Test-Path $componentLink) { Remove-Item -LiteralPath $componentLink -Force }
New-Item -ItemType Directory -Force -Path $componentRoot | Out-Null
New-Item -ItemType Junction -Path $componentLink -Target $SourceDirectory | Out-Null
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
  $sourceImage = Join-Path $project "$build\eh_cp_wifi_scan.bin"
  if (-not (Test-Path $sourceImage)) { throw "Missing C6 application image: $sourceImage" }
  New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
  $image = Join-Path $OutputDirectory $lock.output_name
  Copy-Item $sourceImage $image -Force
} finally { Pop-Location }

$hash = (Get-FileHash $image -Algorithm SHA256).Hash.ToLowerInvariant()
[ordered]@{
  hosted_version = $lock.hosted_version
  source_repository = $lock.source_repository
  source_revision = $lock.source_revision
  source_project = $lock.source_project
  target = $lock.target
  transport = $lock.transport
  board_configuration = 'M5Stack Tab5 internal ESP32-C6; P4 host uses ESP32P4_TAB5_C6_BOARD and qualified 4-bit SDIO at 10 MHz'
  idf_version = $lock.idf_version
  toolchain = (& riscv32-esp-elf-gcc --version | Select-Object -First 1)
  sdkconfig_sha256 = (Get-FileHash (Join-Path $project 'sdkconfig') -Algorithm SHA256).Hash.ToLowerInvariant()
  firmware = (Split-Path $image -Leaf)
  bytes = (Get-Item $image).Length
  sha256 = $hash
} | ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'c6-provenance.json')
Write-Host "HOSTED_C6_BUILD_OK version=$($lock.hosted_version) bytes=$((Get-Item $image).Length) sha256=$hash"
