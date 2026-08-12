[CmdletBinding()]
param(
  [string]$Port = 'COM17',
  [int]$ToneSeconds = 60,
  [int]$FmSeconds = 300,
  [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\audio-validation')
)

$ErrorActionPreference = 'Stop'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$port = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$port.DtrEnable = $false
$port.RtsEnable = $false
$port.NewLine = "`n"
$port.ReadTimeout = 250
$port.WriteTimeout = 1000

function Send-DeviceCommand([string]$Command) {
  Write-Host "> $Command"
  $port.WriteLine($Command)
}

function Test-Sample([object[]]$Samples, [bool]$RequireFm) {
  if ($Samples.Count -eq 0) { throw 'No RTL_AUDIO_TEST_JSON telemetry received.' }
  $last = $Samples[-1]
  if ($last.speaker_enabled -ne 1 -or $last.speaker_running -ne 1) { throw 'Speaker was not enabled/running.' }
  if ($last.sample_rate -ne 48000 -or $last.stereo -ne 1) { throw 'Audio is not 48 kHz stereo.' }
  if ($last.speaker_core -ne 1 -or $last.speaker_priority -ne 6) { throw 'Speaker task is not Core 1 / priority 6.' }
  if (($Samples | Measure-Object ring_overruns -Maximum).Maximum -ne 0) { throw 'PCM ring overrun detected.' }
  if (($Samples | Measure-Object submit_failures -Maximum).Maximum -ne 0) { throw 'M5Unified submission failure detected.' }
  if (($Samples | Measure-Object audio_drops -Maximum).Maximum -ne 0) { throw 'Audio drop detected.' }
  if (($Samples | Measure-Object task_delta -Maximum).Maximum -gt 0) { throw 'Task count increased during test.' }
  if ($RequireFm) {
    if (($Samples | Measure-Object effective_sps -Minimum).Minimum -lt 912000) { throw 'Effective RTL rate below 95% of 960 kS/s.' }
    if (($Samples | Measure-Object usb_overruns -Maximum).Maximum -ne 0) { throw 'USB overrun detected.' }
    if (($Samples | Measure-Object usb_drops -Maximum).Maximum -ne 0) { throw 'RTL consumer drop detected.' }
    if (($Samples | Measure-Object dsp_block_us_max -Maximum).Maximum -gt 13653) { throw 'DSP block exceeded 80% input-block budget.' }
  }
}

function Invoke-AudioPhase([string]$Label, [string]$Command, [int]$Seconds, [bool]$RequireFm) {
  $rawPath = Join-Path $OutputDirectory "$timestamp-$Label.jsonl"
  $samples = [System.Collections.Generic.List[object]]::new()
  $fatal = [System.Collections.Generic.List[string]]::new()
  Send-DeviceCommand $Command
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $line = $port.ReadLine().Trim()
      if (!$line) { continue }
      Add-Content -LiteralPath $rawPath -Value $line
      Write-Host $line
      if ($line.StartsWith('{"type":"rtl_audio_test"')) {
        try { $samples.Add(($line | ConvertFrom-Json)) } catch { $fatal.Add("bad_json:$line") }
      }
      if ($line -match 'Guru Meditation|panic|abort\(|rst:|watchdog|RTL_AUDIO_TEST_.*ERR') { $fatal.Add($line) }
    } catch [System.TimeoutException] { }
  }
  Send-DeviceCommand 'RTL_AUDIO_TEST STOP'
  Start-Sleep -Milliseconds 750
  if ($fatal.Count) { throw "$Label fatal device output: $($fatal -join '; ')" }
  Test-Sample $samples $RequireFm
  [pscustomobject]@{ label = $Label; raw_log = $rawPath; samples = $samples.Count; last = $samples[-1] }
}

try {
  $port.Open()
  Start-Sleep -Milliseconds 500
  while ($port.BytesToRead -gt 0) { try { [void]$port.ReadLine() } catch { break } }

  $builtIn = @(
    Invoke-AudioPhase 'built-in-tone' 'RTL_AUDIO_TEST TONE' $ToneSeconds $false
    Invoke-AudioPhase 'built-in-fm' 'RTL_AUDIO_TEST FM' $FmSeconds $true
  )

  Write-Host 'BUILT-IN AUTOMATED GATES PASS' -ForegroundColor Green
  $audible = Read-Host 'Confirm built-in speaker was audible and clean (yes/no)'
  if ($audible -ne 'yes') { throw 'Built-in speaker manual acceptance failed.' }
  Write-Host 'CONNECT 3.5MM OUTPUT NOW' -ForegroundColor Cyan
  Read-Host 'Connect the 3.5 mm speaker/headphones, then press Enter'

  $headphone = @(
    Invoke-AudioPhase '3.5mm-tone' 'RTL_AUDIO_TEST TONE' $ToneSeconds $false
    Invoke-AudioPhase '3.5mm-fm' 'RTL_AUDIO_TEST FM' $FmSeconds $true
  )
  $audible = Read-Host 'Confirm 3.5 mm output was audible and clean (yes/no)'
  if ($audible -ne 'yes') { throw '3.5 mm manual acceptance failed.' }

  $summary = [pscustomobject]@{
    timestamp = $timestamp
    port = $Port
    result = 'PASS'
    built_in = $builtIn
    output_3_5mm = $headphone
  }
  $summaryPath = Join-Path $OutputDirectory "$timestamp-summary.json"
  $summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding utf8
  Write-Host "PASS: $summaryPath" -ForegroundColor Green
} finally {
  if ($port.IsOpen) { $port.Close() }
  $port.Dispose()
}
