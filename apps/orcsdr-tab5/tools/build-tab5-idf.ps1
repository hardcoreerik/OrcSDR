param(
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4',
  # Opt in to the pinned ESP-Hosted C6 update image, built once and cached.
  # Normal developer builds only need the Tab5/P4 application.
  [switch]$IncludeC6Image,
  # Embed a specifically supplied C6 image instead of building the pinned one.
  [string]$C6Firmware,
  # Backward-compatible explicit switch for P4-only builds (now the default).
  [switch]$WithoutC6,
  # Test build with the Stage-1 DSP A/B harness (RTL_DSP AB ...). Never ship.
  [switch]$DspAb,
  # Compile out per-stage DSP timing (profiler observer-effect runs).
  [switch]$NoDspStageTiming,
  # Test build with the Stage-2 frontend benchmark lab (RTL_DSP LAB ...). Never ship.
  [switch]$DspLab,
  # Enables authenticated C6-only power fault injection. Never ship.
  [switch]$C6FaultTest
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$resolvedC6Firmware = $null
if ($WithoutC6 -and ($C6Firmware -or $IncludeC6Image)) {
  throw 'Use -WithoutC6 or a C6 image option, not both.'
}
if ($C6Firmware -and $IncludeC6Image) {
  throw 'Use -C6Firmware or -IncludeC6Image, not both.'
}
if ($IncludeC6Image) {
  $C6Firmware = & (Join-Path $PSScriptRoot 'resolve-hosted-c6.ps1') -IdfPath $IdfPath
}
if ($C6Firmware) {
  if (-not (Test-Path -LiteralPath $C6Firmware -PathType Leaf)) {
    throw "C6 firmware does not exist: $C6Firmware"
  }
  $resolvedC6Firmware = (Resolve-Path -LiteralPath $C6Firmware).Path
}
if (-not (Test-Path -LiteralPath (Join-Path $IdfPath 'export.ps1'))) {
  throw "ESP-IDF 5.5.4 is not installed at $IdfPath. Install it or use -IdfPath."
}
. (Join-Path $IdfPath 'export.ps1')
Set-Location (Join-Path $PSScriptRoot '..')
$buildDir = 'build-native-hosted3'

# sdkconfig.defaults is the source; regenerate the per-build Kconfig cache so
# a prior transport choice cannot silently survive a configuration change.
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
Copy-Item -LiteralPath 'sdkconfig.defaults' -Destination (Join-Path $buildDir 'sdkconfig') -Force
$configureArgs = @('-B', $buildDir, '-D', "SDKCONFIG=$buildDir/sdkconfig",
                   '-D', 'SDKCONFIG_DEFAULTS=sdkconfig.defaults',
                   '-D', "ORCSDR_DSP_AB=$(if ($DspAb) { 1 } else { 0 })",
                   '-D', "ORCSDR_DSP_STAGE_TIMING=$(if ($NoDspStageTiming) { 0 } else { 1 })",
                   '-D', "ORCSDR_DSP_LAB=$(if ($DspLab) { 1 } else { 0 })",
                   '-D', "ORCSDR_C6_FAULT_TEST=$(if ($C6FaultTest) { 1 } else { 0 })")
# Always set this cache value: switching from an image-embedded build to
# an ordinary P4-only build must not silently retain the previous C6 image.
$configureArgs += @('-D', "C6_FIRMWARE_BIN=$resolvedC6Firmware")
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
  'CONFIG_ESP_HOSTED_HOST_FEAT_PEER_DATA=y',
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
