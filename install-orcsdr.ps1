#requires -Version 5.1
<# Installs final P4 without erasing NVS. C6 replacement requires -UpdateC6. #>
param(
  [string]$Port,
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4',
  [switch]$CheckHostedOnly,
  [switch]$UpdateC6,
  [switch]$DryRun,
  [string]$MockHostedLine,
  [int]$HostedWaitSeconds = 25
)
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$RequiredHosted = '3.0.6'

function Resolve-Port([string]$Requested) {
  if ($Requested) { if ($Requested -notmatch '^COM[0-9]+$') { throw "Port must look like COM17, not '$Requested'." }; return $Requested }
  $ports = @(Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue | Where-Object PNPDeviceID -match 'VID_303A&PID_1001' | ForEach-Object DeviceID)
  if ($ports.Count -eq 1) { return $ports[0] }
  throw 'Specify the Tab5 USB Serial/JTAG port with -Port COM##.'
}
function Test-HostedPair([string]$ComPort) {
  $line = $MockHostedLine
  if (-not $line -and -not $DryRun) {
    $serial = [System.IO.Ports.SerialPort]::new($ComPort, 115200, 'None', 8, 'One')
    try {
      $serial.ReadTimeout = 400; $serial.Open(); $watch = [Diagnostics.Stopwatch]::StartNew()
      while ($watch.Elapsed.TotalSeconds -lt $HostedWaitSeconds) {
        try { $candidate = $serial.ReadLine().Trim() } catch [TimeoutException] { continue }
        if ($candidate -match '^RTL_WIFI_HOSTED\s+') { $line = $candidate; break }
      }
    } finally { if ($serial.IsOpen) { $serial.Close() }; $serial.Dispose() }
  }
  $ok = $line -match "host=$([regex]::Escape($RequiredHosted))" -and $line -match "(slave|coprocessor)=$([regex]::Escape($RequiredHosted))" -and $line -match 'match=1'
  Write-Host "HOSTED_PAIR_CHECK paired=$([int]$ok) line=$line"
  return $ok
}
function Use-Idf {
  if (-not (Test-Path (Join-Path $IdfPath 'export.ps1'))) { throw "ESP-IDF 5.5.4 is required at $IdfPath." }
  $env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.14_env'; $env:PATH = "C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;$env:IDF_PYTHON_ENV_PATH\Scripts;$env:PATH"
  . (Join-Path $IdfPath 'export.ps1')
}
function Flash-App([string]$App, [string]$Build, [string]$ComPort) {
  if ($DryRun) { Write-Host "INSTALL_PLAN app=$App offset=0x10000 preserve_nvs=1"; return }
  Push-Location $App
  try {
    $name = if ($Build -eq 'build-native-hosted3') { 'orcsdr_tab5.bin' } else { 'orcsdr_c6_bridge.bin' }
    & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" -m esptool --chip esp32p4 --port $ComPort --baud 460800 --before default_reset --after hard_reset write_flash 0x10000 (Join-Path $Build $name)
    if ($LASTEXITCODE) { throw 'Application-only flash failed.' }
  } finally { Pop-Location }
}

Set-Location $Root
$script:Port = if ($DryRun -and -not $Port) { 'COM17' } else { Resolve-Port $Port }
Write-Host "OrcSDR installer (Hosted $RequiredHosted, C6 update=$([int][bool]$UpdateC6), dry_run=$([int][bool]$DryRun))"
if ($CheckHostedOnly) { if (Test-HostedPair $script:Port) { exit 0 }; exit 2 }
Use-Idf
if ($UpdateC6) {
  $stage = Join-Path $env:TEMP 'OrcSDR-c6-update'
  if (-not $DryRun) {
    & (Join-Path $Root 'tools\release\build-hosted-c6.ps1') -OutputDirectory $stage -IdfPath $IdfPath
    & (Join-Path $Root 'tools\release\build-c6-bridge.ps1') -C6Firmware (Join-Path $stage 'esp_hosted_tab5_c6.bin') -OutputDirectory $stage -IdfPath $IdfPath
  }
  Flash-App (Join-Path $Root 'apps\orcsdr-c6-bridge') 'build-release-bridge' $script:Port
  if (-not $DryRun) { Start-Sleep -Seconds 12 }
  if (-not (Test-HostedPair $script:Port)) { throw 'C6 bridge did not establish a matching Hosted link. Stop here; use the private M5Burner bridge recovery path.' }
} elseif (-not (Test-HostedPair $script:Port)) {
  Write-Host 'Hosted pair is mismatched. Final P4 flash remains safe, but C6 synchronization is skipped. Re-run with -UpdateC6 or install the private Hosted Bridge through M5Burner first.'
}
$final = Join-Path $Root 'apps\orcsdr-tab5'
if (-not $DryRun) { & (Join-Path $final 'tools\build-tab5-idf.ps1') -IdfPath $IdfPath }
Flash-App $final 'build-native-hosted3' $script:Port
Write-Host 'INSTALL_COMPLETE preserve_nvs=1'
