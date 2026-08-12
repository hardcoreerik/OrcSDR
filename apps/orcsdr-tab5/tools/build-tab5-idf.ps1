param(
  [string]$IdfId = 'esp-idf-5.5.3'
)

$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
. 'C:\Espressif\Initialize-Idf.ps1' -IdfId $IdfId
Set-Location (Join-Path $PSScriptRoot '..')

# Resolve the locked component graph before applying the Tab5 lifecycle patch.
# Arduino's ESP-Hosted adapter must set the Tab5 SDIO pins before initialization.
idf.py reconfigure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$hostInit = Join-Path (Get-Location) 'managed_components\espressif__esp_hosted\host\port\esp\freertos\src\port_esp_hosted_host_init.c'
$source = Get-Content -LiteralPath $hostInit -Raw
 $original = @'
DEFINE_LOG_TAG(host_init);

//ESP_SYSTEM_INIT_FN(esp_hosted_host_init, BIT(0), 120)
static void __attribute__((constructor)) esp_hosted_host_init(void)
{
	ESP_LOGI(TAG, "ESP Hosted : Host chip_ip[%d]", CONFIG_IDF_FIRMWARE_CHIP_ID);
	ESP_ERROR_CHECK(esp_hosted_init());
}

static void __attribute__((destructor)) esp_hosted_host_deinit(void)
{
	ESP_LOGI(TAG, "ESP Hosted deinit");
	esp_hosted_deinit();
}
'@
$replacement = @'
/* OrcSDR owns the 2.12.6 Hosted lifecycle through Arduino WiFi so Tab5 SDIO
 * pins are installed before esp_hosted_init(). Do not restore the constructor. */
'@
if ($source.Contains($original)) {
  Set-Content -LiteralPath $hostInit -Value $source.Replace($original, $replacement) -NoNewline
  Write-Output 'TAB5_HOSTED_LIFECYCLE_PATCH applied version=2.12.6'
} elseif ($source.Contains('DEFINE_LOG_TAG(host_init);') -and
          $source.Contains('OrcSDR owns the 2.12.6 Hosted lifecycle')) {
  Set-Content -LiteralPath $hostInit -Value $source.Replace('DEFINE_LOG_TAG(host_init);', '') -NoNewline
  Write-Output 'TAB5_HOSTED_LIFECYCLE_PATCH repaired version=2.12.6'
} elseif ($source.Contains('OrcSDR owns the 2.12.6 Hosted lifecycle')) {
  Write-Output 'TAB5_HOSTED_LIFECYCLE_PATCH already_applied version=2.12.6'
} else {
  throw 'Unexpected ESP-Hosted 2.12.6 host-init source; refusing to patch.'
}

$m5gfxCommon = Join-Path (Get-Location) 'managed_components\m5stack__m5gfx\src\lgfx\v1\platforms\esp32\common.cpp'
$m5gfxCmake = Join-Path (Get-Location) 'managed_components\m5stack__m5gfx\CMakeLists.txt'
$m5gfxSource = Get-Content -LiteralPath $m5gfxCmake -Raw
if ($m5gfxSource.Contains('# list(APPEND COMPONENT_REQUIRES arduino-esp32)')) {
  Set-Content -LiteralPath $m5gfxCmake -Value $m5gfxSource.Replace(
      '# list(APPEND COMPONENT_REQUIRES arduino-esp32)',
      'list(APPEND COMPONENT_REQUIRES arduino-esp32)') -NoNewline
  Write-Output 'TAB5_M5GFX_ARDUINO_DEP_PATCH applied version=0.2.22'
} elseif ($m5gfxSource.Contains('list(APPEND COMPONENT_REQUIRES arduino-esp32)')) {
  Write-Output 'TAB5_M5GFX_ARDUINO_DEP_PATCH already_applied version=0.2.22'
} else {
  throw 'Unexpected M5GFX 0.2.22 CMake source; refusing to patch.'
}

idf.py reconfigure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $PSScriptRoot 'patch_m5gfx.py') $m5gfxCommon
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

idf.py build
exit $LASTEXITCODE
