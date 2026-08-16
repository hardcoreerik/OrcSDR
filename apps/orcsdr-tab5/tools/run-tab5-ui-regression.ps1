param(
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port = 'COM17',
  [ValidateRange(1, 30)]
  [int]$TimeoutSeconds = 8,
  [switch]$Run
)

$ErrorActionPreference = 'Stop'
$command = if ($Run) { 'RTL_UI_REGRESSION RUN' } else { 'RTL_UI_REGRESSION CHECK' }
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.NewLine = "`n"
$serial.ReadTimeout = 250
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
  $serial.Open()
  Start-Sleep -Milliseconds 250
  $serial.DiscardInBuffer()
  $serial.WriteLine($command)
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $line = $serial.ReadLine().Trim()
      if (!$line) { continue }
      Write-Output $line
      if ($line -notmatch '^RTL_UI_REGRESSION_RESULT ') { continue }
      if ($line -notmatch ' pass=1 ') { throw "UI regression failed: $line" }
      exit 0
    } catch [System.TimeoutException] {}
  }
  throw "Timed out waiting for RTL_UI_REGRESSION_RESULT from $Port."
} finally {
  if ($serial.IsOpen) { $serial.Close() }
}
