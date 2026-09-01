param(
  [string]$Port = 'COM17',
  [int]$BaudRate = 921600,
  [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = 'Stop'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $PSScriptRoot "wifi-release-$stamp.log"
$serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate, 'None', 8, 'One')
$serial.NewLine = "`n"
$serial.ReadTimeout = 250
$serial.Open()

function Wait-Line([string]$Pattern, [int]$Seconds) {
  $deadline = [Diagnostics.Stopwatch]::StartNew()
  while ($deadline.Elapsed.TotalSeconds -lt $Seconds) {
    try { $line = $serial.ReadLine().Trim() } catch [TimeoutException] { continue }
    if (!$line) { continue }
    Add-Content -LiteralPath $logPath -Value $line
    if ($line -match $Pattern) { return $line }
  }
  throw "Timed out waiting for $Pattern"
}

try {
  $serial.DiscardInBuffer()
  $serial.WriteLine('RTL_WIFI_STATUS')
  $status = Wait-Line '^RTL_WIFI_STATUS ' $TimeoutSeconds
  $serial.WriteLine('RTL_WIFI_SCAN')
  if ($status -notmatch 'hosted_match=1') {
    Wait-Line '^RTL_WIFI_HOSTED .* match=1$' $TimeoutSeconds | Out-Null
  }
  $results = Wait-Line '^RTL_WIFI_SCAN_RESULTS count=([0-9]+)$' $TimeoutSeconds
  if ($results -notmatch 'count=([1-9][0-9]*)$') {
    throw "Wi-Fi scan returned no access points: $results"
  }
  Wait-Line '^RTL_WIFI_COEX event=scan_complete ' $TimeoutSeconds | Out-Null
  $serial.WriteLine('RTL_WIFI_STATUS')
  $status = Wait-Line '^RTL_WIFI_STATUS .*hosted_match=1' $TimeoutSeconds
  if ($status -match 'saved_profiles=([1-9][0-9]*)') {
    $serial.WriteLine('RTL_WIFI_CONNECT_SAVED')
    Wait-Line '^RTL_WIFI_COEX event=connect_complete ' $TimeoutSeconds | Out-Null
  } else {
    Add-Content -LiteralPath $logPath -Value 'RTL_WIFI_CONNECT_SKIPPED no_saved_profile'
  }
  Add-Content -LiteralPath $logPath -Value 'RTL_WIFI_RELEASE_TEST PASS'
  Write-Output "PASS $logPath"
} finally {
  if ($serial.IsOpen) { $serial.Close() }
}
