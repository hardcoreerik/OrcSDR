param(
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4'
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'
$env:PATH = "$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
. (Join-Path $IdfPath 'export.ps1')
Set-Location (Join-Path $PSScriptRoot '..')
$buildDir = 'build-native-hosted3'

# sdkconfig.defaults is the source; sdkconfig is a generated cache. Refuse a
# build if the cache contradicts the Tab5 C6 power/SDIO startup configuration.
idf.py -B $buildDir -D "SDKCONFIG=$buildDir/sdkconfig" `
    -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults' reconfigure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

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
  'CONFIG_ESP_HOSTED_HOST_CP_RESET_STRATEGY_ONLY_IF_NECESSARY=y'
)
$config = Get-Content -LiteralPath (Join-Path $buildDir 'sdkconfig')
foreach ($line in $required) {
  if ($config -notcontains $line) { throw "Generated sdkconfig disagrees with defaults: $line" }
}

idf.py -B $buildDir build
exit $LASTEXITCODE
