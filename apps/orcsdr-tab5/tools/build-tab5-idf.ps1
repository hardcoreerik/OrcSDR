param(
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4',
  [string]$C6Firmware
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
$resolvedC6Firmware = $null
if ($C6Firmware) {
  if (-not (Test-Path -LiteralPath $C6Firmware -PathType Leaf)) {
    throw "C6 firmware does not exist: $C6Firmware"
  }
  $resolvedC6Firmware = (Resolve-Path -LiteralPath $C6Firmware).Path
}
. (Join-Path $IdfPath 'export.ps1')
Set-Location (Join-Path $PSScriptRoot '..')
$buildDir = 'build-native-hosted3'

# sdkconfig.defaults is the source; regenerate the per-build Kconfig cache so
# a prior transport choice cannot silently survive a configuration change.
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
Copy-Item -LiteralPath 'sdkconfig.defaults' -Destination (Join-Path $buildDir 'sdkconfig') -Force
$configureArgs = @('-B', $buildDir, '-D', "SDKCONFIG=$buildDir/sdkconfig",
                   '-D', 'SDKCONFIG_DEFAULTS=sdkconfig.defaults')
if ($resolvedC6Firmware) {
  $configureArgs += @('-D', "C6_FIRMWARE_BIN=$resolvedC6Firmware")
}
idf.py @configureArgs reconfigure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The component-manager step above fetches (or re-resolves) managed_components/,
# which can overwrite an already-patched M5GFX checkout. Apply the patch after
# reconfigure, not before, so a fresh checkout/worktree has something to patch
# and a stale patch can't be silently dropped by re-resolution.
& (Join-Path $PSScriptRoot 'apply-m5gfx-tab5-pageflip.ps1')

$required = @(
  'CONFIG_ESP32P4_TAB5_C6_BOARD=y',
  '# CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN is not set',
  'CONFIG_ESP_HOSTED_HOST_RESET_GPIO=15',
  'CONFIG_ESP_HOSTED_HOST_SDIO_PIN_CLK=12',
  'CONFIG_ESP_HOSTED_HOST_SDIO_PIN_CMD=13',
  'CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D0=11',
  'CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D1=10',
  'CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D2=9',
  'CONFIG_ESP_HOSTED_HOST_SDIO_PIN_D3=8',
  'CONFIG_ESP_HOSTED_HOST_CP_RESET_STRATEGY_ONLY_IF_NECESSARY=y',
  'CONFIG_ESP_MAIN_TASK_STACK_SIZE=12288',
  'CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y',
  'CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y',
  'CONFIG_ESP_TASK_WDT_PANIC=y'
)
$config = Get-Content -LiteralPath (Join-Path $buildDir 'sdkconfig')
foreach ($line in $required) {
  if ($config -notcontains $line) { throw "Generated sdkconfig disagrees with defaults: $line" }
}

idf.py -B $buildDir build
if ($LASTEXITCODE -ne 0) { throw "ESP-IDF build failed with exit code $LASTEXITCODE." }
