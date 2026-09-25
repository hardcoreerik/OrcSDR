param(
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4',
  # Defaults to the pinned ESP-Hosted image (built once and cached) so
  # developer builds embed the same C6 update image as releases.
  [string]$C6Firmware,
  # Build without an embedded C6 image; Firmware & Updates then reports
  # the update image as not included.
  [switch]$WithoutC6,
  # Defaults to the pinned OrcMaps world pack (see resolve-orcmaps-world.ps1),
  # flashed into the read-only `orcmaps` partition for the first-run
  # location picker, so dev builds match release images.
  [string]$WorldPack,
  # Build without the world pack; the first-run location step then has no map.
  [switch]$WithoutWorldPack
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
$resolvedC6Firmware = $null
if ($WithoutC6 -and $C6Firmware) { throw 'Use either -C6Firmware or -WithoutC6, not both.' }
if (-not $WithoutC6 -and -not $C6Firmware) {
  $C6Firmware = & (Join-Path $PSScriptRoot 'resolve-hosted-c6.ps1') -IdfPath $IdfPath
}
if ($C6Firmware) {
  if (-not (Test-Path -LiteralPath $C6Firmware -PathType Leaf)) {
    throw "C6 firmware does not exist: $C6Firmware"
  }
  $resolvedC6Firmware = (Resolve-Path -LiteralPath $C6Firmware).Path
}
if ($WithoutWorldPack -and $WorldPack) { throw 'Use either -WorldPack or -WithoutWorldPack, not both.' }
$resolvedWorldPack = ''
if (-not $WithoutWorldPack) {
  $resolvedWorldPack = & (Join-Path $PSScriptRoot 'resolve-orcmaps-world.ps1') -WorldPack $WorldPack
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
# Always passed (empty when opted out) so a previous build's cached path
# cannot silently keep flashing a map.
$configureArgs += @('-D', "ORCMAPS_WORLD_BIN=$resolvedWorldPack")
idf.py @configureArgs reconfigure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The component-manager step above fetches (or re-resolves) managed_components/,
# which can overwrite an already-patched M5GFX checkout. Apply the patch after
# reconfigure, not before, so a fresh checkout/worktree has something to patch
# and a stale patch can't be silently dropped by re-resolution.
& (Join-Path $PSScriptRoot 'apply-m5gfx-tab5-pageflip.ps1')
& (Join-Path $PSScriptRoot 'apply-esp-hosted-trampoline-fix.ps1')

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
  'CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384',
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
