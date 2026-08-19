#requires -Version 5.1
<#
.SYNOPSIS
  Install OrcSDR on a M5Stack Tab5: Python deps, P4 flash, ESP-Hosted pair check.

.DESCRIPTION
  One script for a cloned OrcSDR tree. Builds and flashes the ESP32-P4 image,
  then reads boot logs for RTL_WIFI_HOSTED. The P4 image cannot update the C6;
  a mismatch prints the M5Burner 2.12.6 steps and how to re-check.

.EXAMPLE
  .\install-orcsdr.ps1
.EXAMPLE
  .\install-orcsdr.ps1 -Port COM17
.EXAMPLE
  .\install-orcsdr.ps1 -CheckHostedOnly -Port COM17
#>
param(
  [string]$Port,
  [string]$IdfId = 'esp-idf-5.5.3',
  [switch]$CheckHostedOnly,
  [switch]$SkipPython,
  [int]$HostedWaitSeconds = 25
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
if (-not $Root) { $Root = Get-Location }
Set-Location $Root

$RequiredHosted = '2.12.6'
$yml = Join-Path $Root 'apps\orcsdr-tab5\main\idf_component.yml'
if (Test-Path -LiteralPath $yml) {
  $ymlText = Get-Content -LiteralPath $yml -Raw
  if ($ymlText -match 'esp_hosted:\s*[\r\n]+\s*version:\s*"([^"]+)"') {
    $RequiredHosted = $Matches[1]
  }
}

function Get-Tab5Ports {
  $ports = @()
  Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.PNPDeviceID -match 'VID_303A&PID_1001') {
      $ports += [pscustomobject]@{ DeviceId = $_.DeviceID; Name = $_.Name }
    }
  }
  if ($ports.Count -eq 0) {
    Get-PnpDevice -Class Ports -Status OK -ErrorAction SilentlyContinue | ForEach-Object {
      if ($_.InstanceId -match 'VID_303A&PID_1001' -and $_.FriendlyName -match '(COM[0-9]+)') {
        $ports += [pscustomobject]@{ DeviceId = $Matches[1]; Name = $_.FriendlyName }
      }
    }
  }
  $ports | Sort-Object DeviceId -Unique
}

function Resolve-Port {
  param([string]$Requested)
  if ($Requested) {
    if ($Requested -notmatch '^COM[0-9]+$') { throw "Port must look like COM17, not '$Requested'." }
    return $Requested
  }
  $found = @(Get-Tab5Ports)
  if ($found.Count -eq 1) {
    Write-Host "Detected Tab5 at $($found[0].DeviceId) ($($found[0].Name))."
    return $found[0].DeviceId
  }
  if ($found.Count -gt 1) {
    $list = ($found | ForEach-Object { $_.DeviceId }) -join ', '
    throw "Several Tab5 ports: $list. Re-run with -Port COM??"
  }
  throw 'No Tab5 USB Serial/JTAG port (VID 303A PID 1001). Plug the Tab5 into the PC USB port used for flashing.'
}

function Get-InstallerPython {
  $idfPython = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
  if (Test-Path -LiteralPath $idfPython) { return $idfPython }
  foreach ($name in @('python', 'python3')) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
  }
  throw 'Python not found. Install ESP-IDF 5.5.3 (includes Python) or Python 3.11+.'
}

function Install-PythonRequirements {
  $req = Join-Path $Root 'requirements.txt'
  if (-not (Test-Path -LiteralPath $req)) { throw "Missing $req" }
  $python = Get-InstallerPython
  Write-Host "Python: $python"
  Write-Host 'Installing requirements.txt...'
  & $python -m pip install --upgrade pip | Out-Host
  if ($LASTEXITCODE -ne 0) { throw 'pip upgrade failed' }
  & $python -m pip install -r $req | Out-Host
  if ($LASTEXITCODE -ne 0) { throw 'pip install -r requirements.txt failed' }
}

function Write-HostedFix {
  param([string]$Observed)
  Write-Host ''
  Write-Host 'ESP-Hosted on the Tab5 C6 does not match the P4 app.'
  if ($Observed) { Write-Host "  saw:    $Observed" }
  Write-Host "  needed: host=$RequiredHosted slave=$RequiredHosted match=1"
  Write-Host ''
  Write-Host 'The P4 image cannot update the C6. Finish the pair with M5Burner:'
  Write-Host '  1. https://docs.m5stack.com/en/download  (M5Burner)'
  Write-Host '  2. Connect the Tab5. Choose the Tab5 ESP32-C6 / ESP-Hosted package.'
  Write-Host "  3. Burn ESP-Hosted $RequiredHosted (same numbers as the P4 host library)."
  Write-Host '     Do not use a newer or older C6 package than the P4 pin.'
  Write-Host '  4. Re-check without rebuilding:'
  Write-Host "       .\install-orcsdr.ps1 -CheckHostedOnly -Port $script:Port"
  Write-Host ''
  Write-Host 'Radio still works without a match; Wi-Fi stays blocked until the pair is even.'
}

function Test-HostedPair {
  param(
    [Parameter(Mandatory = $true)][string]$ComPort,
    [int]$Seconds = 25
  )
  $serial = $null
  $saw = $null
  try {
    $serial = [System.IO.Ports.SerialPort]::new($ComPort, 115200, 'None', 8, 'One')
    $serial.NewLine = "`n"
    $serial.ReadTimeout = 400
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Open()
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($deadline.Elapsed.TotalSeconds -lt $Seconds) {
      try { $line = $serial.ReadLine() } catch [TimeoutException] { continue }
      if (-not $line) { continue }
      $line = $line.Trim()
      if ($line -match '^RTL_WIFI_HOSTED\s+(.*)$') {
        $saw = $line
        Write-Host $line
        break
      }
    }
  } finally {
    if ($serial -and $serial.IsOpen) { $serial.Close() }
    if ($serial) { $serial.Dispose() }
  }

  if (-not $saw) {
    Write-Host "No RTL_WIFI_HOSTED line on $ComPort in ${Seconds}s."
    Write-Host 'Power-cycle the Tab5 and re-run: .\install-orcsdr.ps1 -CheckHostedOnly'
    return $false
  }
  $ok = ($saw -match "host=$([regex]::Escape($RequiredHosted))" -and
         $saw -match "slave=$([regex]::Escape($RequiredHosted))" -and
         $saw -match 'match=1')
  if ($ok) {
    Write-Host "ESP-Hosted pair ok ($RequiredHosted)."
    return $true
  }
  Write-HostedFix -Observed $saw
  return $false
}

Write-Host "OrcSDR installer  (ESP-Hosted pin $RequiredHosted)"
Write-Host "Tree: $Root"

if (-not $SkipPython) { Install-PythonRequirements }

$script:Port = Resolve-Port -Requested $Port
Write-Host "Tab5 port: $($script:Port)"

if (-not $CheckHostedOnly) {
  $idfInit = 'C:\Espressif\Initialize-Idf.ps1'
  if (-not (Test-Path -LiteralPath $idfInit)) {
    throw 'ESP-IDF 5.5.3 is not installed. Install from https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32p4/get-started/windows-setup.html then re-run.'
  }
  $flash = Join-Path $Root 'apps\orcsdr-tab5\tools\install-tab5.ps1'
  Write-Host "Building and flashing P4 via $flash ..."
  & $flash -Port $script:Port -IdfId $IdfId
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  Write-Host 'Waiting for Tab5 reboot...'
  Start-Sleep -Seconds 8
}

Write-Host "Checking ESP-Hosted host/slave on $($script:Port)..."
$paired = Test-HostedPair -ComPort $script:Port -Seconds $HostedWaitSeconds
if ($paired) {
  Write-Host ''
  Write-Host 'Install complete. Unplug this PC USB cable for living-room use,'
  Write-Host 'connect the RTL-SDR to the Tab5 USB Host port, and power the Tab5.'
  exit 0
}
exit 2
