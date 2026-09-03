#requires -Version 5.1
<# Builds the temporary P4 bridge around a reproducibly built C6 image. #>
param(
  [Parameter(Mandatory)] [string]$C6Firmware,
  [Parameter(Mandatory)] [string]$OutputDirectory,
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4'
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $C6Firmware)) { throw "Missing C6 firmware: $C6Firmware" }
$C6Firmware = (Resolve-Path $C6Firmware).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$app = Join-Path $repo 'apps\orcsdr-c6-bridge'
$build = 'build-release-bridge'
$env:PYTHONUTF8 = '1'; $env:PYTHONIOENCODING = 'utf-8'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
$env:PATH = "C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. (Join-Path $IdfPath 'export.ps1')
Push-Location $app
try {
  New-Item -ItemType Directory -Force -Path $build | Out-Null
  Copy-Item sdkconfig.defaults (Join-Path $build 'sdkconfig') -Force
  idf.py -B $build -D "SDKCONFIG=$build/sdkconfig" -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults' -D "C6_FIRMWARE_BIN=$C6Firmware" reconfigure
  if ($LASTEXITCODE) { throw 'C6 bridge configuration failed.' }
  idf.py -B $build build
  if ($LASTEXITCODE) { throw 'C6 bridge build failed.' }
  idf.py -B $build merge-bin --format raw --output (Join-Path $OutputDirectory 'OrcSDR-Hosted-Bridge.bin')
  if ($LASTEXITCODE) { throw 'C6 bridge merge-bin failed.' }
  Copy-Item (Join-Path $build 'orcsdr_c6_bridge.bin') (Join-Path $OutputDirectory 'orcsdr_c6_bridge.bin') -Force
  Copy-Item (Join-Path $build 'bootloader\bootloader.bin') (Join-Path $OutputDirectory 'bootloader_0x2000.bin') -Force
  Copy-Item (Join-Path $build 'partition_table\partition-table.bin') (Join-Path $OutputDirectory 'partition-table_0x8000.bin') -Force
} finally { Pop-Location }
Write-Host "C6_BRIDGE_BUILD_OK output=$OutputDirectory"
