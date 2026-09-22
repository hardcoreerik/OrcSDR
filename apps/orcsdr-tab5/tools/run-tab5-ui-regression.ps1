param(
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port = 'COM17',
  [ValidateRange(1, 30)]
  [int]$TimeoutSeconds = 8,
  [ValidateRange(0, 10000)]
  [int]$Cycles = 0,
  [ValidateRange(1, 30)]
  [int]$DwellSeconds = 2,
  [ValidateRange(1, 1000)]
  [int]$WifiEvery = 10,
  [string]$PairingKeyPath = (Join-Path $PSScriptRoot '..\..\..\.orclink\ui-doc.key'),
  [switch]$Run,
  [switch]$Soak,
  [ValidateSet('Smoke', 'Stress', 'Overnight')]
  [string]$Profile,
  [int]$Seed = 0,
  [string]$LogPath,
  [switch]$SelfCheck,
  [switch]$Driver080Rc3,
  [switch]$ResetDevice,
  [switch]$WifiOnly,
  [switch]$WifiCoexistence,
  [switch]$WifiCoexistenceDiagnostic,
  [switch]$DataOnly,
  [switch]$C6Update,
  [switch]$RadioScan,
  [switch]$AmBroadcast,
  [switch]$GainSweep,
  [switch]$IqDiagnostic,
  [switch]$IqHotTune,
  [switch]$SdSelfCheck,
  [switch]$SdBenchmark,
  [ValidateRange(4, 64)]
  [int]$SdBenchmarkMiB = 32,
  [ValidatePattern('^[A-Za-z0-9_-]{1,31}$')]
  [string]$IqTransition = 'manual',
  [ValidateSet('FM', 'AM', 'BROWSE')]
  [string]$IqBand = 'FM',
  [ValidateRange(24000, 1766000000)]
  [uint32]$IqFrequency = 99100000,
  [ValidateRange(0, 496)]
  [Nullable[int]]$IqGainTenthDb,
  [string]$IqOutputPath,
  [string]$IqAntenna,
  [string]$IqAntennaSuitability,
  [ValidateSet('FM', 'BROWSE', 'LORA')]
  [string]$GainSweepBand = 'FM',
  [ValidateRange(24000000, 1766000000)]
  [uint32]$GainSweepFrequency = 99100000,
  [ValidateRange(0, 496)]
  [int[]]$GainSweepGains,
  [ValidateRange(100, 30000)]
  [int]$GainSweepDwellMs = 500,
  [switch]$InstallLaneMap,
  [switch]$InstallFaaAircraft,
  [string]$LocationQuery = '97401',
  [switch]$RequireWifiConnection,
  [switch]$TestBiasTee
)

$ErrorActionPreference = 'Stop'
$script:serial = $null
$script:linesSeen = 0
$script:soakLogPath = $null
$script:lastV3cRfState = $null

if ($Profile) {
  $Soak = $true
  if ($Cycles -eq 0) {
    $Cycles = switch ($Profile) { 'Smoke' { 5 } 'Stress' { 50 } 'Overnight' { 500 } }
  }
  if ($Seed -eq 0) { $Seed = [Environment]::TickCount }
  if (!$LogPath) {
    $artifactDir = Join-Path $PSScriptRoot '..\..\..\artifacts\ui-soak'
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $LogPath = Join-Path $artifactDir "$timestamp-$($Profile.ToLowerInvariant())-seed$Seed.log"
  }
}
if ($RadioScan) {
  if ($Cycles -eq 0) { $Cycles = 10 }
  if (!$LogPath) {
    $artifactDir = Join-Path $PSScriptRoot '..\..\..\artifacts\radio-scan-soak'
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $LogPath = Join-Path $artifactDir "$timestamp-cycles$Cycles.log"
  }
}
if ($LogPath) {
  $parent = Split-Path -Parent $LogPath
  if ($parent) { [void](New-Item -ItemType Directory -Force $parent) }
  $script:soakLogPath = [IO.Path]::GetFullPath($LogPath)
}
if ($Soak -and $Cycles -eq 0) { $Cycles = 10 }

function Write-SoakLine([string]$Line) {
  Write-Host $Line
  if ($script:soakLogPath) { Add-Content -LiteralPath $script:soakLogPath -Value $Line }
}

function Test-FatalLine([string]$Line) {
  return $Line -match '(?i)Guru Meditation|panic(?:ked|\x27ed)?|assert failed|abort\(|task watchdog|interrupt wdt|brownout detector|ESP-ROM:esp32p4|rst:0x'
}

function Read-MatchingLine([string]$Pattern, [int]$Seconds = $TimeoutSeconds) {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  $nextKeepalive = [DateTime]::UtcNow.AddSeconds(1)
  while ([DateTime]::UtcNow -lt $deadline) {
    if ([DateTime]::UtcNow -ge $nextKeepalive) {
      # Long Wi-Fi operations can outlive the device's authenticated serial session.
      $script:serial.WriteLine('PING')
      $nextKeepalive = [DateTime]::UtcNow.AddSeconds(1)
    }
    try {
      $line = $script:serial.ReadLine().Trim()
      if (!$line) { continue }
      $script:linesSeen++
      Write-SoakLine $line
      if ($line -match 'V3C_RF_STATE ') { $script:lastV3cRfState = $line }
      if (Test-FatalLine $line) { throw "Device crash/reset detected: $line" }
      if ($line -match $Pattern) { return $line }
    } catch [System.TimeoutException] {}
  }
  throw "Timed out waiting for device response: $Pattern"
}

function Send-And-Wait([string]$Command, [string]$Pattern, [int]$Seconds = $TimeoutSeconds) {
  $script:serial.WriteLine($Command)
  return Read-MatchingLine $Pattern $Seconds
}

function Read-BinaryProtocolLine($Stream, [int]$Seconds = $TimeoutSeconds) {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  $bytes = [Collections.Generic.List[byte]]::new()
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $value = $Stream.ReadByte()
      if ($value -lt 0) { continue }
      if ($value -eq 10) { return [Text.Encoding]::ASCII.GetString($bytes.ToArray()) }
      if ($value -ne 13) { $bytes.Add([byte]$value) }
      if ($bytes.Count -gt 4096) { throw 'Binary protocol line exceeded 4096 bytes.' }
    } catch [System.TimeoutException] {}
  }
  throw 'Timed out waiting for binary protocol line.'
}

function Read-BinaryMatchingLine([string]$Pattern, [int]$Seconds = $TimeoutSeconds) {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    $remaining = [Math]::Max(1, [int][Math]::Ceiling(($deadline - [DateTime]::UtcNow).TotalSeconds))
    $line = (Read-BinaryProtocolLine $script:serial $remaining).Trim()
    if (!$line) { continue }
    $script:linesSeen++
    if ($line -notmatch '^RTL_IQ_GET_DATA bytes=') { Write-SoakLine $line }
    if ($line -match 'V3C_RF_STATE ') { $script:lastV3cRfState = $line }
    if (Test-FatalLine $line) { throw "Device crash/reset detected: $line" }
    if ($line -match $Pattern) { return $line }
  }
  throw "Timed out waiting for binary device response: $Pattern"
}

function Send-And-WaitBinary([string]$Command, [string]$Pattern,
                            [int]$Seconds = $TimeoutSeconds) {
  $script:serial.WriteLine($Command)
  return Read-BinaryMatchingLine $Pattern $Seconds
}

function Wait-DeviceReady([int]$Seconds = 60, [int]$MinimumUptimeMs = 0) {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  $nextProbe = [DateTime]::MinValue
  while ([DateTime]::UtcNow -lt $deadline) {
    if ([DateTime]::UtcNow -ge $nextProbe) {
      $script:serial.WriteLine('RTL_HEALTH')
      $nextProbe = [DateTime]::UtcNow.AddSeconds(1)
    }
    try {
      $line = $script:serial.ReadLine().Trim()
      if (!$line) { continue }
      $script:linesSeen++
      Write-SoakLine $line
      if (Test-FatalLine $line) { throw "Device crash/reset detected: $line" }
      if ($line -match '^RTL_HEALTH_STATUS uptime_ms=(\d+)' -and
          [int64]$Matches[1] -ge $MinimumUptimeMs) { return }
    } catch [System.TimeoutException] {}
  }
  throw 'Timed out waiting for device readiness.'
}

function Drain-SerialOutput([int]$QuietMilliseconds = 300, [int]$MaximumMilliseconds = 2500) {
  $deadline = [DateTime]::UtcNow.AddMilliseconds($MaximumMilliseconds)
  $quietUntil = [DateTime]::UtcNow.AddMilliseconds($QuietMilliseconds)
  while ([DateTime]::UtcNow -lt $deadline -and [DateTime]::UtcNow -lt $quietUntil) {
    try {
      $line = $script:serial.ReadLine().Trim()
      if (!$line) { continue }
      $script:linesSeen++
      Write-SoakLine $line
      if (Test-FatalLine $line) { throw "Device crash/reset detected: $line" }
      $quietUntil = [DateTime]::UtcNow.AddMilliseconds($QuietMilliseconds)
    } catch [System.TimeoutException] {}
  }
}

function Connect-Authenticated {
  $keyFile = (Resolve-Path -LiteralPath $PairingKeyPath).Path
  $keyText = [IO.File]::ReadAllText($keyFile).Trim()
  if ($keyText -notmatch '^[0-9A-Fa-f]{64}$') {
    throw 'Pairing key must contain exactly 32 hexadecimal bytes.'
  }
  $key = [Convert]::FromHexString($keyText)
  $pair = Send-And-Wait ('PAIR ' + $keyText) '^PAIR_(?:OK|LOCKED|INVALID)$'
  if ($pair -ne 'PAIR_OK') { throw "Device pairing failed: $pair" }

  $nonce = [byte[]]::new(16)
  [Security.Cryptography.RandomNumberGenerator]::Fill($nonce)
  $hmac = [Security.Cryptography.HMACSHA256]::new($key)
  try {
    $hostProof = $hmac.ComputeHash([byte[]]([Text.Encoding]::ASCII.GetBytes('host') + $nonce))
    $reply = Send-And-Wait (
      'AUTH ' + [Convert]::ToHexString($nonce) + ' ' + [Convert]::ToHexString($hostProof)
    ) '^AUTH_(?:OK|DENIED|ERROR|INVALID)'
    if ($reply -notmatch '^AUTH_OK ([0-9A-Fa-f]{64})$') {
      throw "Device authentication failed: $reply"
    }
    $expected = $hmac.ComputeHash([byte[]]([Text.Encoding]::ASCII.GetBytes('device') + $nonce))
    if (-not [Security.Cryptography.CryptographicOperations]::FixedTimeEquals(
        [Convert]::FromHexString($Matches[1]), $expected)) {
      throw 'Device authentication proof did not match.'
    }
  } finally {
    $hmac.Dispose()
  }
  Write-SoakLine 'RTL_UI_SOAK_AUTH verified=1'
  Drain-SerialOutput
}

function Test-SdBenchmarkFailureLine([string]$Line) {
  return $Line -match '^RTL_SD_BENCH_(?:ERROR|INVALID)\b'
}

function Invoke-SdBenchmark {
  $script:serial.WriteLine("RTL_SD_BENCH $SdBenchmarkMiB")
  $deadline = [DateTime]::UtcNow.AddMinutes(5)
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $line = $script:serial.ReadLine().Trim()
      if (!$line) { continue }
      Write-SoakLine $line
      if (Test-FatalLine $line) { throw "Device crash/reset detected: $line" }
      if (Test-SdBenchmarkFailureLine $line) { throw "SD benchmark rejected: $line" }
      if ($line -match '^RTL_SD_BENCH_DONE pass=([01])$') {
        if ($Matches[1] -ne '1') { throw 'SD benchmark failed.' }
        return
      }
    } catch [System.TimeoutException] {}
  }
  throw 'Timed out waiting for SD benchmark.'
}

function Get-UiState {
  $line = Send-And-Wait 'RTL_UI STATUS' '^RTL_UI_STATUS '
  if ($line -notmatch 'screen=(\S+) band=(\S+) frequency_hz=(\d+) settings=([01]) fm=([01]) am=([01]) p25=([01]) adsb=([01]) lora=([01]) rf24=([01]) home_font=([01]) graphics=([01])') {
    throw "Malformed UI status: $line"
  }
  return [pscustomobject]@{
    Screen = $Matches[1].ToUpperInvariant()
    Band = $Matches[2].ToUpperInvariant()
    Frequency = [uint32]$Matches[3]
    Active = @([int]$Matches[4], [int]$Matches[5], [int]$Matches[6], [int]$Matches[7], [int]$Matches[8], [int]$Matches[9], [int]$Matches[10])
    HomeFont = [int]$Matches[11]
    Graphics = [int]$Matches[12]
  }
}

function Test-ExclusiveScreen($State, [string]$Screen) {
  # Wi-Fi Analysis owns the display while the prior radio stream continues in the background.
  if ($Screen -eq 'WIFI_ANALYSIS') {
    return $State.Active[0] -eq 0 -and $State.Active[6] -eq 1
  }
  $expected = switch ($Screen) {
    'FM' { @(0,1,0,0,0,0,0) }
    'AM' { @(0,0,1,0,0,0,0) }
    'P25' { @(0,0,0,1,0,0,0) }
    'ADSB' { @(0,0,0,0,1,0,0) }
    'LORA' { @(0,0,0,0,0,1,0) }
    'SETTINGS' { @(1,0,0,0,0,0,0) }
    'HOME' { @(0,0,0,0,0,0,0) }
    default { return $true }
  }
  return ($State.Active -join ',') -eq ($expected -join ',')
}

function Wait-UiState([string]$Screen, [string]$Band) {
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
  do {
    $script:serial.WriteLine('PING')
    $state = Get-UiState
    if ($state.Screen -eq $Screen -and $state.Band -eq $Band -and
        ($Screen -eq 'ADSB' -or $state.HomeFont -eq 1) -and
        (Test-ExclusiveScreen $state $Screen)) {
      return $state
    }
    Start-Sleep -Milliseconds 250
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "UI state did not reach exclusive screen=$Screen band=$Band; last=$($state.Screen)/$($state.Band) active=$($state.Active -join ',')"
}

function Open-Ui([string]$Target, [string]$ExpectedBand) {
  [void](Send-And-Wait "RTL_UI OPEN $Target" "^RTL_UI_OPEN_OK target=$Target$")
  return Wait-UiState $Target $ExpectedBand
}

function Watch-Responsive([int]$Seconds, [string]$Screen, [string]$Band) {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    $script:serial.WriteLine('PING')
    [void](Wait-UiState $Screen $Band)
    Start-Sleep -Milliseconds 500
  }
}

function Get-AudioStatus {
  $line = Send-And-Wait 'RTL_AUDIO_TEST STATUS' '^\{"type":"rtl_audio_test"'
  try { return $line | ConvertFrom-Json } catch { throw "Malformed audio status: $line" }
}

function ConvertFrom-SignalStatus([string]$Line) {
  if ($line -notmatch '^RTL_SIGNAL_STATUS band=(\S+) frequency_hz=(\d+) signal_dbfs_tenths=(-?\d+) .*filter_hz=(\d+) lo_nudge=(-?\d+)$') {
    throw "Malformed signal status: $line"
  }
  return [pscustomobject]@{
    Band = $Matches[1]
    Frequency = [uint32]$Matches[2]
    SignalTenths = [int]$Matches[3]
    FilterHz = [uint32]$Matches[4]
    Line = $line
  }
}

function Get-SignalStatus {
  for ($attempt = 0; $attempt -lt 3; $attempt++) {
    $line = Send-And-Wait 'RTL_SIGNAL' '^RTL_SIGNAL_STATUS '
    try { return ConvertFrom-SignalStatus $line } catch {
      if ($attempt -eq 2) { throw }
      Start-Sleep -Milliseconds 100
    }
  }
}

function Assert-FmAudioProgress {
  $streamDeadline = [DateTime]::UtcNow.AddSeconds(30)
  do {
    $driver = Get-DriverStatus
    if ($driver.State -eq 'STREAMING' -and $driver.Bytes -gt 0) { break }
    Start-Sleep -Milliseconds 500
  } while ([DateTime]::UtcNow -lt $streamDeadline)
  if ($driver.State -ne 'STREAMING' -or $driver.Bytes -eq 0) {
    throw "FM IQ did not start: state=$($driver.State) bytes=$($driver.Bytes)"
  }
  $first = Get-AudioStatus
  $deadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(5, $TimeoutSeconds))
  do {
    $script:serial.WriteLine('PING')
    Start-Sleep -Seconds 1
    $next = Get-AudioStatus
    if ($next.speaker_enabled -eq 1 -and $next.speaker_running -eq 1 -and
        [uint64]$next.audio_chunks -gt [uint64]$first.audio_chunks) {
      Write-SoakLine "RTL_UI_SOAK_AUDIO pass=1 chunks_before=$($first.audio_chunks) chunks_after=$($next.audio_chunks)"
      return
    }
    if ([uint64]$next.audio_chunks -lt [uint64]$first.audio_chunks) { $first = $next }
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "FM audio did not recover: enabled=$($next.speaker_enabled) running=$($next.speaker_running) chunks_before=$($first.audio_chunks) chunks_after=$($next.audio_chunks)"
}

function ConvertFrom-HealthStatus([string]$Line) {
  if ($Line -notmatch '^RTL_HEALTH_STATUS uptime_ms=(\d+) free_heap=(\d+) min_free_heap=(\d+) dma_free=(\d+) dma_min=(\d+) dma_largest=(\d+) tasks=(\d+) main_stack_hwm=(\d+) reset_reason=(\d+)$') {
    throw "Malformed health status: $Line"
  }
  return [pscustomobject]@{
    Line = $Line
    UptimeMs = [uint64]$Matches[1]
    FreeHeap = [uint64]$Matches[2]
    MinFreeHeap = [uint64]$Matches[3]
    DmaFree = [uint64]$Matches[4]
    DmaMin = [uint64]$Matches[5]
    DmaLargest = [uint64]$Matches[6]
    Tasks = [uint32]$Matches[7]
    MainStackHwm = [uint32]$Matches[8]
    ResetReason = [uint32]$Matches[9]
  }
}

function Get-HealthStatus {
  return ConvertFrom-HealthStatus (Send-And-Wait 'RTL_HEALTH' '^RTL_HEALTH_STATUS ')
}

function Test-UptimeAdvanced([uint64]$Previous, [uint64]$Current) {
  $elapsed = ($Current + 0x100000000L - $Previous) % 0x100000000L
  return $elapsed -gt 0 -and $elapsed -lt 0x80000000L
}

function Assert-HealthStatus($Health) {
  if ($Health.FreeHeap -eq 0 -or $Health.DmaFree -eq 0 -or
      $Health.DmaLargest -eq 0 -or $Health.Tasks -eq 0 -or $Health.MainStackHwm -eq 0) {
    throw "Device reported exhausted memory or stack: $($Health.Line)"
  }
}

function Assert-Health {
  $health = Get-HealthStatus
  Assert-HealthStatus $health
}

function ConvertFrom-RadioFrequencyStatus([string]$Line) {
  if ($Line -notmatch '^RTL_FREQ_STATUS band=(\S+) frequency_hz=(\d+) mode=(.+)$') {
    throw "Malformed frequency status: $Line"
  }
  return [pscustomobject]@{
    Band = $Matches[1].ToUpperInvariant()
    Frequency = [uint32]$Matches[2]
    Mode = $Matches[3]
  }
}

function Get-RadioFrequency {
  return ConvertFrom-RadioFrequencyStatus (Send-And-Wait 'RTL_FREQ' '^RTL_FREQ_STATUS ')
}

function Assert-SoundCycle {
  [void](Send-And-Wait 'RTL_SOUND OFF' '^RTL_SOUND_OK enabled=0$')
  [void](Send-And-Wait 'RTL_SOUND' '^RTL_SOUND_STATUS enabled=0$')
  [void](Send-And-Wait 'RTL_SOUND ON' '^RTL_SOUND_OK enabled=1$')
  [void](Send-And-Wait 'RTL_SOUND' '^RTL_SOUND_STATUS enabled=1$')
  Assert-FmAudioProgress
  Write-SoakLine 'RTL_UI_SOAK_SOUND pass=1'
}

function Assert-WifiCycle {
  [void](Send-And-Wait 'RTL_UI ACTION SETTINGS WIFI_POWER 0' '^RTL_UI_ACTION_OK$' 20)
  [void](Send-And-Wait 'RTL_WIFI_STATUS' '^RTL_WIFI_STATUS station=0 .*connected=0 ' 20)
  [void](Send-And-Wait 'RTL_UI ACTION SETTINGS WIFI_POWER 1' '^RTL_UI_ACTION_OK$' 20)
  [void](Send-And-Wait 'RTL_WIFI_SCAN' '^RTL_WIFI_SCAN_QUEUED$')
  $deadline = [DateTime]::UtcNow.AddSeconds(45)
  do {
    Start-Sleep -Seconds 1
    $status = Send-And-Wait 'RTL_WIFI_STATUS' '^RTL_WIFI_STATUS ' 10
    if ($status -match 'station=1 .*scanning=0 ') { break }
  } while ([DateTime]::UtcNow -lt $deadline)
  if ($status -notmatch 'station=1 .*scanning=0 ') { throw "Wi-Fi scan did not finish: $status" }
  if ($status -match 'saved_profiles=0') { throw 'Wi-Fi cycle requires one saved profile.' }
  [void](Send-And-Wait 'RTL_WIFI_CONNECT_SAVED' '^RTL_WIFI_CONNECT_(?:QUEUED|ERROR)' 10)
  $deadline = [DateTime]::UtcNow.AddSeconds(45)
  do {
    Start-Sleep -Seconds 1
    $status = Send-And-Wait 'RTL_WIFI_STATUS' '^RTL_WIFI_STATUS ' 10
    if ($status -match 'connected=1 ') { break }
  } while ([DateTime]::UtcNow -lt $deadline)
  if ($status -notmatch 'connected=1 ') { throw "Wi-Fi did not reconnect: $status" }
  Assert-Health
  Write-SoakLine 'RTL_UI_SOAK_WIFI pass=1'
}

function Get-WifiStatus {
  $line = Send-And-Wait 'RTL_WIFI_STATUS' '^RTL_WIFI_STATUS '
  if ($line -notmatch 'station=([01]).*connected=([01]).*saved_profiles=(\d+).*power=([01]).*auto_connect=([01]).*antenna=(internal|external).*ap_count=(\d+)') {
    throw "Malformed Wi-Fi status: $line"
  }
  return [pscustomobject]@{
    Line = $line
    Station = [int]$Matches[1]
    Connected = [int]$Matches[2]
    Profiles = [int]$Matches[3]
    Power = [int]$Matches[4]
    AutoConnect = [int]$Matches[5]
    Antenna = $Matches[6]
    AccessPoints = [int]$Matches[7]
  }
}

function Reset-DeviceBaseline {
  Wait-DeviceReady 60
  Connect-Authenticated
  [void](Send-And-Wait 'RTL_RESET' '^RTL_RESETTING$')
  Write-SoakLine 'RTL_UI_SOAK_RESET serial=1'
  Start-Sleep -Seconds 1
  $script:serial.DiscardInBuffer()
  Wait-DeviceReady 60 11000
  Write-SoakLine 'RTL_UI_SOAK_RESET_RESULT pass=1'
}

function Get-WifiCoexStatus {
  $line = Send-And-Wait 'RTL_WIFI_COEX_STATUS' '^RTL_WIFI_COEX_STATUS '
  if ($line -notmatch 'station=([01]).*scanning=([01]).*connecting=([01]).*connected=([01]).*connect_pause=([01]).*rtl_ready=([01]).*capture_state=(\d+).*capture_requested=([01]).*band=(\S+).*frequency_hz=(\d+).*audio_enabled=([01]).*speaker_running=([01]).*audio_chunks=(\d+).*audio_drops=(\d+)') {
    throw "Malformed Wi-Fi coexistence status: $line"
  }
  return [pscustomobject]@{
    Line = $line; Station = [int]$Matches[1]; Scanning = [int]$Matches[2]
    Connecting = [int]$Matches[3]; Connected = [int]$Matches[4]; ConnectPause = [int]$Matches[5]
    RtlReady = [int]$Matches[6]; CaptureState = [int]$Matches[7]; CaptureRequested = [int]$Matches[8]; Band = $Matches[9].ToUpperInvariant()
    Frequency = [uint32]$Matches[10]; AudioEnabled = [int]$Matches[11]; SpeakerRunning = [int]$Matches[12]
    AudioChunks = [uint64]$Matches[13]; AudioDrops = [uint64]$Matches[14]
  }
}

function Assert-WifiCoexAudio([string]$Step, [uint64]$DropBaseline) {
  Assert-FmAudioProgress
  $status = Get-WifiCoexStatus
  if ($status.RtlReady -ne 1 -or $status.CaptureState -ne 3 -or $status.Band -ne 'FM' -or
      $status.AudioEnabled -ne 1 -or $status.SpeakerRunning -ne 1 -or
      $status.AudioDrops -gt $DropBaseline) {
    throw "Wi-Fi coexistence failed at ${Step}: $($status.Line)"
  }
  Write-SoakLine "RTL_WIFI_COEX_TEST step=$Step pass=1 chunks=$($status.AudioChunks) drops=$($status.AudioDrops) drop_baseline=$DropBaseline"
}

function Assert-WifiCoexistence($initialUi) {
  $initialWifi = Get-WifiStatus
  if ($initialWifi.Power -ne 1) { throw 'Wi-Fi coexistence requires Connectivity Power to be on.' }
  if ($initialWifi.Profiles -eq 0) { throw 'Wi-Fi coexistence requires one saved Wi-Fi profile.' }
  $restoreSoundOff = $false
  try {
    [void](Open-Ui 'FM' 'FM')
    $restoreSoundOff = (Send-And-Wait 'RTL_SOUND' '^RTL_SOUND_STATUS enabled=[01]$').EndsWith('0')
    if ($restoreSoundOff) {
      [void](Send-And-Wait 'RTL_SOUND ON' '^RTL_SOUND_OK enabled=1$')
    }
    $dropBaseline = (Get-WifiCoexStatus).AudioDrops
    Assert-WifiCoexAudio 'fm_baseline' $dropBaseline
    if ($initialWifi.Connected -eq 1) {
      $dropBaseline = (Get-WifiCoexStatus).AudioDrops
      [void](Send-And-Wait 'RTL_WIFI_DISCONNECT' '^RTL_WIFI_DISCONNECT_OK$')
      if ((Get-WifiStatus).Connected -ne 0) { throw 'Wi-Fi disconnect did not complete.' }
      Assert-WifiCoexAudio 'disconnect' $dropBaseline
    }
    $dropBaseline = (Get-WifiCoexStatus).AudioDrops
    [void](Send-And-Wait 'RTL_WIFI_CONNECT_SAVED' '^RTL_WIFI_CONNECT_QUEUED ' 10)
    [void](Read-MatchingLine '^RTL_WIFI_COEX event=connect_complete ' 45)
    if ((Get-WifiStatus).Connected -ne 1) { throw 'Saved Wi-Fi profile did not connect.' }
    Assert-WifiCoexAudio 'connect' $dropBaseline
    [void](Open-Ui 'WIFI_ANALYSIS' 'FM')
    [void](Send-And-Wait 'RTL_RF24_PAGE 1' '^RTL_RF24_PAGE_OK page=1$')
    $dropBaseline = (Get-WifiCoexStatus).AudioDrops
    [void](Send-And-Wait 'RTL_WIFI_SCAN' '^RTL_WIFI_SCAN_QUEUED$')
    [void](Wait-WifiScan)
    Assert-WifiCoexAudio 'rf24_scan' $dropBaseline
    $dropBaseline = (Get-WifiCoexStatus).AudioDrops
    [void](Open-Ui 'FM' 'FM')
    Assert-WifiCoexAudio 'fm_return' $dropBaseline
    Assert-Health
    Write-SoakLine 'RTL_WIFI_COEX_TEST_RESULT pass=1 evidence=fm+connect+rf24_scan+return'
  } finally {
    try {
      $currentWifi = Get-WifiStatus
      if ($initialWifi.Connected -eq 0 -and $currentWifi.Connected -eq 1) {
        [void](Send-And-Wait 'RTL_WIFI_DISCONNECT' '^RTL_WIFI_DISCONNECT_OK$')
      } elseif ($initialWifi.Connected -eq 1 -and $currentWifi.Connected -eq 0) {
        Connect-Authenticated
        $restored = Wait-WifiConnectOutcome
        if ($restored -notmatch 'event=connect_complete ') { throw "Could not restore Wi-Fi: $restored" }
      }
      try {
        [void](Open-Ui $initialUi.Screen $initialUi.Band)
      } finally {
        if ($restoreSoundOff) { [void](Send-And-Wait 'RTL_SOUND OFF' '^RTL_SOUND_OK enabled=0$') }
      }
    } catch { Write-Warning "Could not restore initial Wi-Fi/UI state: $($_.Exception.Message)" }
  }
}

function Wait-WifiConnectOutcome {
  [void](Send-And-Wait 'RTL_WIFI_CONNECT_SAVED PAUSE' '^RTL_WIFI_CONNECT_QUEUED saved_profile=0 mode=pause$' 10)
  return Read-MatchingLine '^RTL_WIFI_COEX event=connect_(?:complete|failed|start_failed) ' 45
}

function Assert-WifiCoexistenceDiagnostic($initialUi) {
  $initialWifi = Get-WifiStatus
  if ($initialWifi.Power -ne 1 -or $initialWifi.Profiles -eq 0) {
    throw 'Wi-Fi diagnostic requires Connectivity Power on and a saved profile.'
  }
  $initialFrequency = $initialUi.Frequency
  try {
    [void](Send-And-Wait 'RTL_TUNE FM 96100000' '^RTL_TUNE_OK band=FM frequency_hz=96100000$')
    $dropBaseline = (Get-WifiCoexStatus).AudioDrops
    Assert-WifiCoexAudio 'fm_961_baseline' $dropBaseline
    if ((Get-WifiStatus).Connected -eq 1) {
      Connect-Authenticated
      [void](Send-And-Wait 'RTL_WIFI_DISCONNECT' '^RTL_WIFI_DISCONNECT_OK$')
    }
    $dropBaseline = (Get-WifiCoexStatus).AudioDrops
    $paused = Wait-WifiConnectOutcome
    if ($paused -notmatch 'event=connect_complete ') {
      throw "Paused Wi-Fi comparison did not connect: $paused"
    }
    Assert-WifiCoexAudio 'paused_connect_restored' $dropBaseline
    $dropBaseline = (Get-WifiCoexStatus).AudioDrops
    [void](Send-And-Wait 'RTL_WIFI_SCAN' '^RTL_WIFI_SCAN_QUEUED$')
    [void](Wait-WifiScan)
    Assert-WifiCoexAudio 'paused_connect_scan' $dropBaseline
    Connect-Authenticated
    [void](Send-And-Wait 'RTL_WIFI_DISCONNECT' '^RTL_WIFI_DISCONNECT_OK$')
    $dropBaseline = (Get-WifiCoexStatus).AudioDrops
    $reconnected = Wait-WifiConnectOutcome
    if ($reconnected -notmatch 'event=connect_complete ') {
      throw "Second paused Wi-Fi connection did not connect: $reconnected"
    }
    Assert-WifiCoexAudio 'second_paused_connect' $dropBaseline
    Assert-Health
    Write-SoakLine 'RTL_WIFI_COEX_DIAGNOSTIC_RESULT pass=1 fm_hz=96100000 paused_connects=2 scan=1'
  } finally {
    try {
      Connect-Authenticated
      $currentWifi = Get-WifiStatus
      if ($initialWifi.Connected -eq 0 -and $currentWifi.Connected -eq 1) {
        [void](Send-And-Wait 'RTL_WIFI_DISCONNECT' '^RTL_WIFI_DISCONNECT_OK$')
      } elseif ($initialWifi.Connected -eq 1 -and $currentWifi.Connected -eq 0) {
        $restored = Wait-WifiConnectOutcome
        if ($restored -notmatch 'event=connect_complete ') { throw "Could not restore Wi-Fi: $restored" }
      }
      [void](Send-And-Wait "RTL_TUNE $($initialUi.Band) $initialFrequency" "^RTL_TUNE_OK band=$($initialUi.Band) ")
      [void](Open-Ui $initialUi.Screen $initialUi.Band)
    } catch { Write-Warning "Could not restore Wi-Fi/UI state: $($_.Exception.Message)" }
  }
}

function Wait-WifiScan {
  $result = Read-MatchingLine '^RTL_WIFI_SCAN_RESULTS count=([0-9]+)$' 45
  if ($result -notmatch 'count=([1-9][0-9]*)$') { throw "Wi-Fi scan returned no APs: $result" }
  $count = [int]$Matches[1]
  [void](Read-MatchingLine '^RTL_WIFI_COEX event=scan_complete ' 10)
  return $count
}

function Assert-WifiLists([int]$ExpectedAccessPoints) {
  $begin = Send-And-Wait 'RTL_WIFI_RESULTS' '^RTL_WIFI_RESULTS_BEGIN count=([0-9]+) total=([0-9]+) revision=([0-9]+) age_s=([0-9]+) duration_ms=([0-9]+)$'
  if ($begin -notmatch 'count=([0-9]+) ' -or [int]$Matches[1] -ne $ExpectedAccessPoints) {
    throw "Wi-Fi result count changed: $begin"
  }
  for ($i = 0; $i -lt $ExpectedAccessPoints; $i++) {
    [void](Read-MatchingLine "^RTL_WIFI_AP index=$i ssid_hex=[0-9A-Fa-f]* bssid=[0-9A-Fa-f]{12} rssi=-?[0-9]+ channel=[0-9]+ secure=[01] security=[A-Z0-9/_-]+ phy=[A-Za-z0-9/_-]+ ht40=[01]$")
  }
  [void](Read-MatchingLine '^RTL_WIFI_RESULTS_END$')

  $profiles = Send-And-Wait 'RTL_WIFI_PROFILES' '^RTL_WIFI_PROFILES_BEGIN count=([0-9]+)$'
  if ($profiles -notmatch 'count=([0-9]+)$') { throw "Malformed Wi-Fi profiles: $profiles" }
  $profileCount = [int]$Matches[1]
  for ($i = 0; $i -lt $profileCount; $i++) {
    [void](Read-MatchingLine "^RTL_WIFI_PROFILE index=$i ssid_hex=[0-9A-Fa-f]+ connected=[01]$")
  }
  [void](Read-MatchingLine '^RTL_WIFI_PROFILES_END$')
  return $profileCount
}

function Assert-WifiCli($initialUi) {
  $initial = Get-WifiStatus
  try {
    [void](Send-And-Wait 'SET_WIFI 00 00 00' '^WIFI_INVALID$')
    [void](Send-And-Wait 'RTL_UI ACTION SETTINGS CONNECT_SAVED 99' '^RTL_UI_ACTION_INVALID profile_index$')
    [void](Send-And-Wait 'RTL_UI ACTION SETTINGS FORGET 99' '^RTL_UI_ACTION_INVALID profile_index$')
    [void](Send-And-Wait 'RTL_UI ACTION SETTINGS MOVE_UP 0' '^RTL_UI_ACTION_INVALID profile_move$')

    [void](Send-And-Wait "RTL_UI ACTION SETTINGS WIFI_BOOT $(1 - $initial.AutoConnect)" '^RTL_UI_ACTION_OK$')
    if ((Get-WifiStatus).AutoConnect -eq $initial.AutoConnect) { throw 'Auto-connect toggle did not apply.' }
    [void](Send-And-Wait "RTL_UI ACTION SETTINGS WIFI_BOOT $($initial.AutoConnect)" '^RTL_UI_ACTION_OK$')

    $otherAntenna = if ($initial.Antenna -eq 'internal') { 1 } else { 0 }
    [void](Send-And-Wait "RTL_UI ACTION SETTINGS ANTENNA $otherAntenna" '^RTL_UI_ACTION_OK$')
    if ((Get-WifiStatus).Antenna -eq $initial.Antenna) { throw 'Antenna toggle did not apply.' }
    [void](Send-And-Wait "RTL_UI ACTION SETTINGS ANTENNA $(if ($initial.Antenna -eq 'external') { 1 } else { 0 })" '^RTL_UI_ACTION_OK$')

    [void](Send-And-Wait 'RTL_UI ACTION SETTINGS WIFI_POWER 0' '^RTL_UI_ACTION_OK$' 20)
    $off = Get-WifiStatus
    if ($off.Power -ne 0 -or $off.Station -ne 0 -or $off.Connected -ne 0) {
      throw "Wi-Fi power-off state is inconsistent: $($off.Line)"
    }
    [void](Send-And-Wait 'RTL_UI ACTION SETTINGS WIFI_POWER 1' '^RTL_UI_ACTION_OK$' 20)

    [void](Open-Ui 'WIFI_ANALYSIS' $initialUi.Band)
    $autoCount = Wait-WifiScan
    [void](Send-And-Wait 'RTL_WIFI_SCAN' '^RTL_WIFI_SCAN_QUEUED$')
    $manualCount = Wait-WifiScan
    $profileCount = Assert-WifiLists $manualCount

    [void](Send-And-Wait 'RTL_WIFI_DISCONNECT' '^RTL_WIFI_DISCONNECT_OK$')
    if ((Get-WifiStatus).Connected -ne 0) { throw 'Wi-Fi disconnect did not apply.' }
    if ($profileCount -gt 0) {
      [void](Send-And-Wait 'RTL_UI ACTION SETTINGS CONNECT_SAVED 0' '^RTL_UI_ACTION_OK$')
      [void](Read-MatchingLine '^RTL_WIFI_COEX event=connect_complete ' 45)
      if ((Get-WifiStatus).Connected -ne 1) { throw 'Saved Wi-Fi profile did not connect.' }
    } elseif ($RequireWifiConnection) {
      throw 'Connection proof required, but the device has no saved Wi-Fi profile.'
    } else {
      Write-SoakLine 'RTL_WIFI_CLI_CONNECT skipped=no_saved_profile'
    }
    Assert-Health
    Connect-Authenticated
    [void](Open-Ui 'HOME' $initialUi.Band)
    Write-SoakLine "RTL_WIFI_CLI_RESULT pass=1 auto_aps=$autoCount manual_aps=$manualCount profiles=$profileCount connected=$([int]($profileCount -gt 0))"
  } finally {
    try {
      Connect-Authenticated
      [void](Send-And-Wait "RTL_UI ACTION SETTINGS WIFI_BOOT $($initial.AutoConnect)" '^RTL_UI_ACTION_OK$')
      [void](Send-And-Wait "RTL_UI ACTION SETTINGS ANTENNA $(if ($initial.Antenna -eq 'external') { 1 } else { 0 })" '^RTL_UI_ACTION_OK$')
      [void](Send-And-Wait "RTL_UI ACTION SETTINGS WIFI_POWER $($initial.Power)" '^RTL_UI_ACTION_OK$' 20)
      [void](Send-And-Wait "RTL_UI OPEN $($initialUi.Screen)" '^RTL_UI_OPEN_(?:OK|INVALID)')
    } catch { Write-Warning "Could not restore initial Wi-Fi/UI state: $($_.Exception.Message)" }
  }
}

function Wait-CatalogIdle([int]$Seconds, [switch]$RequireProgress) {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  $lastProgress = -1
  $sawIntermediateProgress = $false
  do {
    Start-Sleep -Seconds 1
    $status = Send-And-Wait 'RTL_CATALOG_STATUS' '^RTL_CATALOG_STATUS ' 10
    if ($status -notmatch 'busy=([01]) operation=\d+ progress=(\d+)') {
      throw "Malformed catalog status: $status"
    }
    $busy = [int]$Matches[1]
    $progress = [int]$Matches[2]
    if ($busy -eq 1) {
      if ($lastProgress -ge 0 -and $progress -lt $lastProgress) {
        throw "Catalog progress moved backwards: $lastProgress -> $progress"
      }
      if ($progress -gt 0 -and $progress -lt 100) { $sawIntermediateProgress = $true }
      $lastProgress = $progress
    } else {
      if ($RequireProgress -and !$sawIntermediateProgress) {
        throw "Catalog operation completed without observable progress: $status"
      }
      return $status
    }
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "Catalog operation timed out: $status"
}

function Wait-DriverStreaming([int]$Seconds = 30) {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  do {
    $driver = Get-DriverStatus
    if ($driver.State -eq 'STREAMING' -and $driver.Bytes -gt 0) { return $driver }
    Start-Sleep -Milliseconds 500
  } while ([DateTime]::UtcNow -lt $deadline)
  throw "Radio did not resume streaming: state=$($driver.State) bytes=$($driver.Bytes)"
}

function Assert-DataServices {
  $wifi = Get-WifiStatus
  if ($wifi.Power -eq 0) {
    [void](Send-And-Wait 'RTL_UI ACTION SETTINGS WIFI_POWER 1' '^RTL_UI_ACTION_OK$' 20)
    $wifi = Get-WifiStatus
  }
  if ($wifi.Connected -eq 0) {
    if ($wifi.Profiles -eq 0) { throw 'Data service test requires a saved Wi-Fi profile.' }
    for ($attempt = 1; $attempt -le 3; $attempt++) {
      Connect-Authenticated
      [void](Send-And-Wait 'RTL_UI ACTION SETTINGS CONNECT_SAVED 0' '^RTL_UI_ACTION_OK$')
      $event = Read-MatchingLine '^RTL_WIFI_COEX event=connect_(?:complete|failed) ' 45
      if ($event -match 'event=connect_complete') { break }
    }
  }
  if ((Get-WifiStatus).Connected -ne 1) { throw 'Data service test could not connect Wi-Fi.' }

  [void](Send-And-Wait 'RTL_CATALOG_CHECK' '^RTL_CATALOG_CHECK_QUEUED$')
  $catalog = Wait-CatalogIdle 60
  if ($catalog -notmatch 'ready=1 .*message="Catalog verified"') {
    throw "Catalog verification failed: $catalog"
  }
  if ($InstallLaneMap) {
    Connect-Authenticated
    [void](Send-And-Wait 'RTL_CATALOG_INSTALL lane_county_map' '^RTL_CATALOG_INSTALL_QUEUED$')
    $catalog = Wait-CatalogIdle 180
    if ($catalog -notmatch 'message="Pack installed and verified"') {
      throw "Lane County map install failed: $catalog"
    }
    $pack = Read-MatchingLine '^RTL_CATALOG_PACK id=lane_county_map ' 10
    if ($pack -notmatch 'installed=1 update=0 status="INSTALLED"') {
      throw "Lane County map was not activated: $pack"
    }
  }
  if ($InstallFaaAircraft) {
    Connect-Authenticated
    [void](Open-Ui 'FM' 'FM')
    $before = Wait-DriverStreaming 30
    [void](Send-And-Wait 'RTL_CATALOG_INSTALL faa_aircraft' '^RTL_CATALOG_INSTALL_QUEUED$')
    $catalog = Wait-CatalogIdle 900 -RequireProgress
    if ($catalog -notmatch 'message="Pack installed and verified"') {
      throw "FAA aircraft install failed: $catalog"
    }
    $pack = Send-And-Wait 'RTL_CATALOG_LIST' '^RTL_CATALOG_PACK id=faa_aircraft ' 10
    if ($pack -notmatch 'installed=1 update=0 status="INSTALLED"') {
      throw "FAA aircraft pack was not activated: $pack"
    }
    $after = Wait-DriverStreaming 30
    if ($after.Overruns -gt $before.Overruns -or $after.Drops -gt $before.Drops) {
      throw "Radio resumed with new transport loss: overruns=$($before.Overruns)->$($after.Overruns) drops=$($before.Drops)->$($after.Drops)"
    }
    Write-SoakLine "RTL_CATALOG_FAA_RESULT pass=1 bytes_before=$($before.Bytes) bytes_after=$($after.Bytes)"
  }

  if ($LocationQuery -notmatch '^[\x20-\x7E]{1,63}$') {
    throw 'LocationQuery must contain 1-63 printable ASCII characters.'
  }
  Connect-Authenticated
  [void](Send-And-Wait "RTL_LOCATION LOOKUP $LocationQuery" '^RTL_LOCATION_LOOKUP_QUEUED$')
  $deadline = [DateTime]::UtcNow.AddSeconds(30)
  do {
    Start-Sleep -Seconds 1
    $location = Send-And-Wait 'RTL_LOCATION STATUS' '^RTL_LOCATION_STATUS ' 10
    if ($location -match 'busy=0') { break }
  } while ([DateTime]::UtcNow -lt $deadline)
  if ($location -notmatch 'busy=0 ready=1 latitude_e7=-?\d+ longitude_e7=-?\d+ message="Address found; confirm to save"') {
    throw "Location lookup failed: $location"
  }
  Assert-Health
  Write-SoakLine "RTL_DATA_SERVICES_RESULT pass=1 catalog=verified lane_map_installed=$([int][bool]$InstallLaneMap) faa_aircraft_installed=$([int][bool]$InstallFaaAircraft) location_query=$LocationQuery"
}

function Capture-ResetEvidence {
  Write-SoakLine 'RTL_UI_SOAK_RECOVERY begin=1'
  $deadline = [DateTime]::UtcNow.AddSeconds(20)
  $nextProbe = [DateTime]::MinValue
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      if (!$script:serial.IsOpen) { $script:serial.Open() }
      if ([DateTime]::UtcNow -ge $nextProbe) {
        $script:serial.WriteLine('RTL_HEALTH')
        $nextProbe = [DateTime]::UtcNow.AddSeconds(2)
      }
      $line = $script:serial.ReadLine().Trim()
      if ($line) {
        Write-SoakLine $line
        if ($line -match '^RTL_HEALTH_STATUS ') {
          Write-SoakLine 'RTL_UI_SOAK_RECOVERY device_responsive=1'
          return
        }
        if ($line -match '^RTL_RESET_REASON ' -or $line -match '^RTL_SERIAL_VERBOSITY ') { return }
      }
    } catch [System.TimeoutException] {
    } catch {
      if ($script:serial.IsOpen) { $script:serial.Close() }
      Start-Sleep -Milliseconds 500
    }
  }
  Write-SoakLine 'RTL_UI_SOAK_RECOVERY device_responsive=0 reset_evidence=unavailable'
}

function ConvertFrom-DriverStatus([string]$Line) {
  $pattern = 'version=(\S+) state=(\S+) profile=(\d+) profile_name="([^"]+)" provisional=([01]) device_caps=(0x[0-9a-fA-F]+) library_caps=(0x[0-9a-fA-F]+).*gain_auto_cap=([01]) rtl_agc_cap=([01]) gain_cap=([01]) bias_cap=([01]) mode=(AUTO|MANUAL) gain_tenth_db=(\d+) rtl_agc=([01]) bias=([01]) bytes=(\d+) blocks=(\d+) effective_sps=(\d+) overruns=(\d+) drops=(\d+) shadow_ok=([01]) metrics_ok=([01]) frequency_hz=(\d+) frequency_ok=([01]) route=(\S+)'
  if ($Line -notmatch $pattern) { throw "Malformed driver status: $Line" }
  [pscustomobject]@{
    Version = $Matches[1]; State = $Matches[2]; Profile = [int]$Matches[3]
    ProfileName = $Matches[4]; Provisional = [int]$Matches[5]
    DeviceCaps = [Convert]::ToUInt32($Matches[6].Substring(2), 16)
    LibraryCaps = [Convert]::ToUInt32($Matches[7].Substring(2), 16)
    GainAutoCap = [int]$Matches[8]; RtlAgcCap = [int]$Matches[9]
    GainCap = [int]$Matches[10]; BiasCap = [int]$Matches[11]
    Mode = $Matches[12]; Gain = [int]$Matches[13]; RtlAgc = [int]$Matches[14]
    Bias = [int]$Matches[15]; Bytes = [uint64]$Matches[16]; Blocks = [uint64]$Matches[17]
    EffectiveSps = [uint32]$Matches[18]; Overruns = [uint32]$Matches[19]
    Drops = [uint32]$Matches[20]; ShadowOk = [int]$Matches[21]; MetricsOk = [int]$Matches[22]
    Frequency = [uint32]$Matches[23]; FrequencyOk = [int]$Matches[24]; Route = $Matches[25]
  }
}

function ConvertFrom-IqDiagnosticStart([string]$Line) {
  if ($Line -notmatch '^RTL_IQ_DIAG_START transition="([A-Za-z0-9_-]{1,31})" sequence=(\d+) bytes=(\d+) rate=(\d+) frequency_hz=(\d+) started_ms=(\d+)$') {
    throw "Malformed IQ diagnostic start: $Line"
  }
  [pscustomobject]@{
    Transition = $Matches[1]
    Sequence = [uint32]$Matches[2]
    Bytes = [uint32]$Matches[3]
    Rate = [uint32]$Matches[4]
    Frequency = [uint32]$Matches[5]
    StartedMs = [uint32]$Matches[6]
  }
}

function ConvertFrom-V3cRfState([string]$Line) {
  $pattern = '^(?:I \(\d+\) esp_rtl_sdr: )?V3C_RF_STATE profile=(\S+) rf=(\d+) rate=(\d+) mode=(\S+) direct=(ON|OFF) pll_if=(\d+) demod_if=(\d+) nco=(\S+) input=(\S+) gain_mode=(AUTO|MANUAL) gain_tenth_db=(-?\d+) rtl_agc=([01]) short_transfers=(\d+) transition=(\S+)$'
  if ($Line -notmatch $pattern) { throw "Malformed V3c RF state: $Line" }
  [pscustomobject]@{
    Profile = $Matches[1]; Frequency = [uint32]$Matches[2]; Rate = [uint32]$Matches[3]
    Mode = $Matches[4]; Direct = $Matches[5] -eq 'ON'; PllIf = [uint32]$Matches[6]
    DemodIf = [uint32]$Matches[7]; Nco = $Matches[8]; Input = $Matches[9]
    GainMode = $Matches[10]; Gain = [int]$Matches[11]; RtlAgc = [int]$Matches[12]
    ShortTransfers = [uint32]$Matches[13]; Transition = $Matches[14]
  }
}

function Test-IqDriverReady($Before, $Current, [uint64]$ExpectedFrequency,
                             [bool]$RestartObserved, [bool]$HotTune = $false) {
  if ($Current.State -ne 'STREAMING' -or $Current.Frequency -ne $ExpectedFrequency -or
      $Current.Bytes -le 0) { return $false }
  if ($HotTune) {
    return $Before.Frequency -ne $ExpectedFrequency -and $Current.Bytes -gt $Before.Bytes
  }
  return $RestartObserved -or $Before.State -ne 'STREAMING' -or
         $Current.Bytes -lt $Before.Bytes
}

function Test-IqGainApplied($Driver, [int]$ExpectedGain) {
  return $Driver.State -eq 'STREAMING' -and $Driver.Mode -eq 'MANUAL' -and
         $Driver.Gain -eq $ExpectedGain
}

function Invoke-SelfCheck {
  if (-not (Test-FatalLine 'Guru Meditation Error: Core 1 panic')) { throw 'Fatal parser missed panic.' }
  if (-not (Test-FatalLine 'ESP-ROM:esp32p4-eco2-20240710')) { throw 'Fatal parser missed reset.' }
  if (Test-FatalLine 'RTL_UI_STATUS screen=home band=FM frequency_hz=96144000') {
    throw 'Fatal parser rejected healthy telemetry.'
  }
  $audio = '{"type":"rtl_audio_test","speaker_enabled":1,"speaker_running":1,"headphone_connected":1,"internal_speaker_muted":1,"audio_chunks":42}' | ConvertFrom-Json
  if ($audio.speaker_running -ne 1 -or $audio.audio_chunks -ne 42 -or
      $audio.headphone_connected -ne 1 -or $audio.internal_speaker_muted -ne 1) {
    throw 'Audio parser failed.'
  }
  $coex = 'RTL_WIFI_COEX_STATUS station=1 scanning=0 connecting=0 connected=1 connect_pause=0 rtl_ready=1 capture_state=3 capture_requested=0 band=FM frequency_hz=96100000 audio_enabled=1 speaker_running=1 audio_chunks=42 audio_drops=0'
  if ($coex -notmatch 'connect_pause=0.*rtl_ready=1.*capture_state=3.*band=FM.*speaker_running=1') { throw 'Coexistence parser failed.' }
  $c6 = 'RTL_WIFI_C6_STATUS host=3.0.6 coprocessor=2.12.6 transport=1 embedded=1 state=ready percent=0 stage=version match=0'
  if ($c6 -notmatch '^RTL_WIFI_C6_STATUS host=\S+ coprocessor=\S+ transport=1 embedded=1 state=ready percent=0 stage=\S+ match=0$') {
    throw 'C6 update parser failed.'
  }
  if (!(Test-SdBenchmarkFailureLine 'RTL_SD_BENCH_ERROR open_failed') -or
      !(Test-SdBenchmarkFailureLine 'RTL_SD_BENCH_INVALID size') -or
      (Test-SdBenchmarkFailureLine 'RTL_SD_BENCH_DONE pass=1')) {
    throw 'SD benchmark failure parser failed.'
  }
  if (-not (Test-ExclusiveScreen ([pscustomobject]@{ Active = @(0,0,0,0,1,0,0) }) 'ADSB')) {
    throw 'Exclusive dashboard check rejected valid ADS-B state.'
  }
  if (Test-ExclusiveScreen ([pscustomobject]@{ Active = @(0,1,0,0,1,0,0) }) 'ADSB') {
    throw 'Exclusive dashboard check accepted stale FM state.'
  }
  if (-not (Test-ExclusiveScreen ([pscustomobject]@{ Active = @(0,0,0,0,0,0,1) }) 'WIFI_ANALYSIS')) {
    throw 'Wi-Fi Analysis check rejected exclusive ownership.'
  }
  if (-not (Test-ExclusiveScreen ([pscustomobject]@{ Active = @(0,1,0,0,0,0,1) }) 'WIFI_ANALYSIS')) {
    throw 'Wi-Fi Analysis check rejected a background FM stream.'
  }
  if (-not (Test-ExclusiveScreen ([pscustomobject]@{ Active = @(0,0,1,0,0,0,0) }) 'AM')) {
    throw 'Exclusive dashboard check rejected valid AM state.'
  }
  $health = ConvertFrom-HealthStatus 'RTL_HEALTH_STATUS uptime_ms=123 free_heap=456 min_free_heap=400 dma_free=300 dma_min=250 dma_largest=200 tasks=12 main_stack_hwm=2048 reset_reason=1'
  Assert-HealthStatus $health
  if ($health.UptimeMs -ne 123 -or $health.DmaLargest -ne 200 -or $health.MainStackHwm -ne 2048) {
    throw 'Health parser failed.'
  }
  if (!(Test-UptimeAdvanced 4294967290 5) -or
      (Test-UptimeAdvanced 5000 100) -or
      (Test-UptimeAdvanced 100 100)) {
    throw 'Uptime rollover check failed.'
  }
  $frequency = ConvertFrom-RadioFrequencyStatus 'RTL_FREQ_STATUS band=P25 frequency_hz=453925000 mode=P25 C4FM'
  if ($frequency.Band -ne 'P25' -or $frequency.Frequency -ne 453925000 -or
      $frequency.Mode -ne 'P25 C4FM') {
    throw 'Radio frequency parser failed.'
  }
  $signal = ConvertFrom-SignalStatus 'RTL_SIGNAL_STATUS band=AM frequency_hz=590000 signal_dbfs_tenths=-321 stereo_locked=0 left_dbfs_tenths=-400 right_dbfs_tenths=-400 rds_carrier=0 rds_signal_tenths=-900 pilot_env_thou=0 filter_hz=6000 lo_nudge=0'
  if ($signal.Band -ne 'AM' -or $signal.Frequency -ne 590000 -or
      $signal.SignalTenths -ne -321 -or $signal.FilterHz -ne 6000) {
    throw 'AM signal parser failed.'
  }
  $driver = ConvertFrom-DriverStatus 'RTL_DRIVER_STATUS installed=1 version=0.8.0-rc3 state=STREAMING profile=2 profile_name="blog_v3_r820t2" provisional=1 device_caps=0x0001fbd9 library_caps=0x000fffff delivery=callback gain_auto_cap=0 rtl_agc_cap=0 gain_cap=1 bias_cap=0 mode=MANUAL gain_tenth_db=297 rtl_agc=0 bias=0 bytes=123456 blocks=42 effective_sps=959488 overruns=0 drops=0 shadow_ok=1 metrics_ok=1 frequency_hz=23999999 frequency_ok=1 route=DIRECT_Q'
  if ($driver.Version -ne '0.8.0-rc3' -or $driver.Profile -ne 2 -or
      $driver.Frequency -ne 23999999 -or $driver.FrequencyOk -ne 1 -or
      $driver.Route -ne 'DIRECT_Q') {
    throw 'Driver acceptance parser failed.'
  }
  $iqDiag = ConvertFrom-IqDiagnosticStart 'RTL_IQ_DIAG_START transition="cold_fm" sequence=7 bytes=4800000 rate=2400000 frequency_hz=99100000 started_ms=1234'
  if ($iqDiag.Transition -ne 'cold_fm' -or $iqDiag.Sequence -ne 7 -or
      $iqDiag.Bytes -ne 4800000 -or $iqDiag.Rate -ne 2400000 -or
      $iqDiag.Frequency -ne 99100000 -or $iqDiag.StartedMs -ne 1234) {
    throw 'IQ diagnostic metadata parser failed.'
  }
  $rfState = ConvertFrom-V3cRfState 'I (1234) esp_rtl_sdr: V3C_RF_STATE profile=blog_v3_r820t2 rf=99113000 rate=2400000 mode=NORMAL_TUNER direct=OFF pll_if=3570000 demod_if=3570000 nco=none input=COMPLEX_IQ gain_mode=MANUAL gain_tenth_db=14 rtl_agc=0 short_transfers=0 transition=COLD_INIT'
  if ($rfState.PllIf -ne 3570000 -or $rfState.DemodIf -ne 3570000 -or
      $rfState.Mode -ne 'NORMAL_TUNER' -or $rfState.Transition -ne 'COLD_INIT') {
    throw 'V3c RF state parser failed.'
  }
  $beforeTune = [pscustomobject]@{ State = 'STREAMING'; Frequency = 99113000; Bytes = 500000000 }
  $staleTune = [pscustomobject]@{ State = 'STREAMING'; Frequency = 99113000; Bytes = 500100000 }
  $freshTune = [pscustomobject]@{ State = 'STREAMING'; Frequency = 99113000; Bytes = 1000000 }
  if ((Test-IqDriverReady $beforeTune $staleTune 99113000 $false) -or
      !(Test-IqDriverReady $beforeTune $freshTune 99113000 $false)) {
    throw 'IQ post-tune restart guard failed.'
  }
  $hotBefore = [pscustomobject]@{ State = 'STREAMING'; Frequency = 23900000; Bytes = 1000000 }
  $hotAfter = [pscustomobject]@{ State = 'STREAMING'; Frequency = 24100000; Bytes = 1100000 }
  if (!(Test-IqDriverReady $hotBefore $hotAfter 24100000 $false $true)) {
    throw 'IQ hot-tune guard failed.'
  }
  $gainReady = [pscustomobject]@{ State = 'STREAMING'; Mode = 'MANUAL'; Gain = 229 }
  if (!(Test-IqGainApplied $gainReady 229) -or (Test-IqGainApplied $gainReady 280)) {
    throw 'IQ explicit-gain guard failed.'
  }
  $lineBytes = [Text.Encoding]::ASCII.GetBytes("RTL_IQ_GET_DATA bytes=4`r`n")
  $protocolBytes = [byte[]]($lineBytes + [byte[]](13, 10, 255, 0))
  $protocolStream = [IO.MemoryStream]::new($protocolBytes)
  if ((Read-BinaryProtocolLine $protocolStream 1) -ne 'RTL_IQ_GET_DATA bytes=4' -or
      $protocolStream.ReadByte() -ne 13) {
    throw 'Binary protocol line reader consumed payload bytes.'
  }
  Write-SoakLine 'RTL_UI_SOAK_SELF_CHECK pass=1'
}

if (($InstallLaneMap -or $InstallFaaAircraft) -and !$DataOnly) {
  throw '-InstallLaneMap and -InstallFaaAircraft require -DataOnly.'
}
if ($IqHotTune -and !$IqDiagnostic) { throw '-IqHotTune requires -IqDiagnostic.' }
if (@($SelfCheck, $Run, $Soak, $Driver080Rc3, $WifiOnly, $WifiCoexistence, $WifiCoexistenceDiagnostic, $DataOnly, $C6Update, $RadioScan, $AmBroadcast, $GainSweep, $IqDiagnostic, $SdSelfCheck, $SdBenchmark).Where({ $_ }).Count -gt 1) {
  throw 'Choose only one primary test mode.'
}
if ($SelfCheck) { Invoke-SelfCheck; exit 0 }

function Get-C6UpdateStatus {
  $line = Send-And-Wait 'RTL_WIFI_C6_STATUS' '^RTL_WIFI_C6_STATUS '
  if ($line -notmatch '^RTL_WIFI_C6_STATUS host=(\S+) coprocessor=(\S+) transport=([01]) embedded=([01]) state=(\S+) percent=([0-9]+) stage=(\S+) match=([01])$') {
    throw "Malformed C6 update status: $line"
  }
  [pscustomobject]@{
    Host = $Matches[1]; Coprocessor = $Matches[2]; Transport = [int]$Matches[3]
    Embedded = [int]$Matches[4]; State = $Matches[5]; Percent = [int]$Matches[6]
    Stage = $Matches[7]; Match = [int]$Matches[8]
  }
}

function Invoke-C6UpdateTest {
  Wait-DeviceReady 60 11000
  Connect-Authenticated
  $before = Get-C6UpdateStatus
  if ($before.Transport -ne 1 -or $before.Embedded -ne 1 -or $before.State -ne 'ready') {
    throw "C6 update requires a reachable mismatched C6 and embedded release image: $($before | ConvertTo-Json -Compress)"
  }
  [void](Send-And-Wait 'RTL_UI ACTION SETTINGS C6_UPDATE_CONFIRM' '^RTL_UI_ACTION_OK$' 10)
  $writing = Get-C6UpdateStatus
  if ($writing.State -notin @('updating', 'rebooting') -or $writing.Percent -gt 100) {
    throw "C6 update did not enter a bounded update state: $($writing | ConvertTo-Json -Compress)"
  }
  Write-SoakLine "RTL_WIFI_C6_UPDATE_TEST queued_via=ui_action host=$($before.Host) coprocessor=$($before.Coprocessor)"
  Wait-DeviceReady 90 11000
  $after = Get-C6UpdateStatus
  if ($after.Match -ne 1 -or $after.Coprocessor -ne '3.0.6' -or $after.State -ne 'current') {
    throw "C6 update verification failed: $($after | ConvertTo-Json -Compress)"
  }
  Write-SoakLine 'RTL_WIFI_C6_UPDATE_TEST pass=1 path=ui_action+status+post_reboot'
}

function Get-DriverStatus {
  for ($attempt = 0; $attempt -lt 3; $attempt++) {
    $line = Send-And-Wait 'RTL_DRIVER STATUS' '^RTL_DRIVER_STATUS '
    try { return ConvertFrom-DriverStatus $line } catch { }
  }
  throw "Malformed driver status: $line"
}

function Read-ExactSerialBytes([IO.Stream]$Output, [Security.Cryptography.IncrementalHash]$Hash, [int]$Count) {
  $buffer = [byte[]]::new([Math]::Min(4096, $Count))
  $remaining = $Count
  $deadline = [DateTime]::UtcNow.AddSeconds(10)
  while ($remaining -gt 0) {
    try {
      $read = $script:serial.Read($buffer, 0, [Math]::Min($buffer.Length, $remaining))
      if ($read -le 0) { continue }
      $Output.Write($buffer, 0, $read)
      $Hash.AppendData($buffer, 0, $read)
      $remaining -= $read
      $deadline = [DateTime]::UtcNow.AddSeconds(10)
    } catch [System.TimeoutException] {
      if ([DateTime]::UtcNow -ge $deadline) {
        throw "Timed out with $remaining raw IQ bytes remaining in chunk."
      }
    }
  }
}

function Invoke-IqDiagnosticCapture {
  if (!$IqAntenna -or !$IqAntennaSuitability) {
    throw '-IqDiagnostic requires -IqAntenna and -IqAntennaSuitability.'
  }
  Wait-DeviceReady 60 11000
  Connect-Authenticated
  $staleCapture = Send-And-Wait 'RTL_IQ_DIAG_STATUS' '^RTL_IQ_DIAG_STATUS ' 10
  if ($staleCapture -match ' active=1 ') { throw 'An IQ diagnostic capture is already active.' }
  if ($staleCapture -match ' ready=1 ') {
    [void](Send-And-Wait 'RTL_IQ_RETRIEVE_END' '^RTL_IQ_RETRIEVE_(?:DONE|RESUMING)$' 10)
  }
  $beforeTune = Get-DriverStatus
  if ($IqHotTune) {
    $current = Get-RadioFrequency
    if ($current.Band -ne $IqBand) {
      throw "Hot IQ tune requires current band $IqBand; device is $($current.Band)."
    }
    $script:lastV3cRfState = $null
    [void](Send-And-Wait "RTL_FREQ $IqFrequency" '^RTL_FREQ_OK ' 20)
  } else {
    [void](Send-And-Wait "RTL_TUNE $IqBand $IqFrequency" '^RTL_TUNE_OK ' 20)
  }
  $expectedDriverFrequency = $IqFrequency
  $restartObserved = $beforeTune.State -ne 'STREAMING'
  $deadline = [DateTime]::UtcNow.AddSeconds(30)
  do {
    $driver = Get-DriverStatus
    if ($driver.State -ne 'STREAMING') { $restartObserved = $true }
    if (Test-IqDriverReady $beforeTune $driver $expectedDriverFrequency $restartObserved $IqHotTune) { break }
    Start-Sleep -Milliseconds 250
  } while ([DateTime]::UtcNow -lt $deadline)
  if (!(Test-IqDriverReady $beforeTune $driver $expectedDriverFrequency $restartObserved $IqHotTune)) {
    throw "Requested IQ state did not stabilize: state=$($driver.State) display_hz=$IqFrequency driver_lo_hz=$($driver.Frequency) expected_lo_hz=$expectedDriverFrequency"
  }
  if ($null -ne $IqGainTenthDb) {
    [void](Send-And-Wait "RTL_DRIVER GAIN $IqGainTenthDb" '^RTL_DRIVER_RESULT .*accepted=1 result=ESP_OK$' 10)
    $gainDeadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
      $driver = Get-DriverStatus
      if (Test-IqGainApplied $driver $IqGainTenthDb) { break }
      Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $gainDeadline)
    if (!(Test-IqGainApplied $driver $IqGainTenthDb)) {
      throw "Requested IQ gain did not apply: mode=$($driver.Mode) gain_tenth_db=$($driver.Gain) expected=$IqGainTenthDb"
    }
  }

  $healthBefore = Get-HealthStatus
  $startLine = Send-And-Wait "RTL_IQ_DIAG_START $IqTransition" '^RTL_IQ_DIAG_(?:START|ERROR) ' 10
  if ($startLine -notmatch '^RTL_IQ_DIAG_START ') { throw "IQ capture rejected: $startLine" }
  $start = ConvertFrom-IqDiagnosticStart $startLine
  $doneLine = Read-MatchingLine '^RTL_IQ_DONE storage=psram source=diagnostic ' 15
  $driverAfterCapture = Get-DriverStatus
  $healthAfterCapture = Get-HealthStatus
  [void](Send-And-Wait 'RTL_STOP' '^RTL_STOP_RESULT ' 10)

  $output = if ($IqOutputPath) {
    [IO.Path]::GetFullPath($IqOutputPath)
  } else {
    $directory = Join-Path $PSScriptRoot '..\..\..\artifacts\v3c-iq-investigation'
    [void](New-Item -ItemType Directory -Force $directory)
    Join-Path ([IO.Path]::GetFullPath($directory)) ((Get-Date -Format 'yyyyMMdd-HHmmss') + "-$IqTransition.cu8")
  }
  $parent = Split-Path -Parent $output
  if ($parent) { [void](New-Item -ItemType Directory -Force $parent) }
  if (Test-Path -LiteralPath $output) { throw "IQ output already exists: $output" }
  $partial = "$output.partial"

  $ready = Send-And-Wait 'RTL_IQ_GET_BEGIN' '^RTL_IQ_GET_(?:READY|ERROR) ' 10
  if ($ready -notmatch '^RTL_IQ_GET_READY chunk=(\d+) bytes=(\d+)$') {
    throw "IQ retrieval could not begin: $ready"
  }
  $chunkBytes = [int]$Matches[1]
  $totalBytes = [int]$Matches[2]
  if ($totalBytes -ne $start.Bytes) {
    throw "IQ byte count changed: start=$($start.Bytes) retrieve=$totalBytes"
  }

  $hash = [Security.Cryptography.IncrementalHash]::CreateHash(
      [Security.Cryptography.HashAlgorithmName]::SHA256)
  $stream = [IO.File]::Open($partial, [IO.FileMode]::Create,
                            [IO.FileAccess]::Write, [IO.FileShare]::None)
  try {
    $remaining = $totalBytes
    while ($remaining -gt 0) {
      $line = Send-And-WaitBinary 'RTL_IQ_GET_CHUNK' '^RTL_IQ_GET_(?:DATA|ERROR) ' 10
      if ($line -notmatch '^RTL_IQ_GET_DATA bytes=(\d+)$') {
        throw "IQ retrieval failed: $line"
      }
      $count = [int]$Matches[1]
      if ($count -le 0 -or $count -gt $chunkBytes -or $count -gt $remaining) {
        throw "Invalid IQ chunk size: $count"
      }
      Read-ExactSerialBytes $stream $hash $count
      $remaining -= $count
    }
  } finally {
    $stream.Dispose()
  }
  $localSha = [Convert]::ToHexString($hash.GetHashAndReset()).ToLowerInvariant()
  $hash.Dispose()
  $finish = Read-BinaryMatchingLine '^RTL_IQ_GET_DONE ' 10
  if ($finish -notmatch '^RTL_IQ_GET_DONE bytes=(\d+) sha256=([0-9a-fA-F]{64})$' -or
      [int]$Matches[1] -ne $totalBytes -or $Matches[2].ToLowerInvariant() -ne $localSha) {
    throw "IQ retrieval hash mismatch: local=$localSha device='$finish'"
  }
  Move-Item -LiteralPath $partial -Destination $output
  [void](Send-And-Wait 'RTL_IQ_RETRIEVE_END' '^RTL_IQ_RETRIEVE_(?:DONE|RESUMING)$' 10)

  $driverAfterRetrieval = Get-DriverStatus
  $healthAfterRetrieval = Get-HealthStatus
  $rfState = if ($script:lastV3cRfState) {
    ConvertFrom-V3cRfState $script:lastV3cRfState
  } else { $null }
  $metadata = [ordered]@{
    timestamp_utc = [DateTime]::UtcNow.ToString('o')
    capture_path = $output
    transition = $start.Transition
    tune_method = if ($IqHotTune) { 'hot' } else { 'restart' }
    sequence = $start.Sequence
    capture_started_ms = $start.StartedMs
    dongle_profile = $driverAfterCapture.ProfileName
    driver_version = $driverAfterCapture.Version
    driver_base_commit = 'e1ca40e04f8140245d56837cd149bf901f771441'
    driver_instrumentation_commit = '94ecd187070b39f370c5d7f0dc55a56762dd6c86'
    requested_display_rf_hz = $IqFrequency
    reported_driver_lo_hz = $driverAfterCapture.Frequency
    expected_driver_lo_hz = $expectedDriverFrequency
    sample_rate_sps = $start.Rate
    tuner_mode = $driverAfterCapture.Route
    direct_sampling = $driverAfterCapture.Route -eq 'DIRECT_Q'
    driver_rf_state = $script:lastV3cRfState
    pll_if_hz = if ($rfState) { $rfState.PllIf } else { $null }
    demod_if_hz = if ($rfState) { $rfState.DemodIf } else { $null }
    demod_nco = if ($rfState) { $rfState.Nco } else { $null }
    rtl_input = if ($rfState) { $rfState.Input } else { $null }
    gain_mode = $driverAfterCapture.Mode
    gain_tenth_db = $driverAfterCapture.Gain
    rtl_agc = $driverAfterCapture.RtlAgc
    usb_overruns_before = $driver.Overruns
    usb_overruns_after = $driverAfterCapture.Overruns
    consumer_drops_before = $driver.Drops
    consumer_drops_after = $driverAfterCapture.Drops
    short_transfers = if ($rfState) { $rfState.ShortTransfers } else { $null }
    short_transfers_note = if ($rfState) { 'driver counter at most recent RF transition' } else { 'not exposed for this profile' }
    capture_bytes = $totalBytes
    capture_sha256 = $localSha
    antenna = $IqAntenna
    antenna_band_suitability = $IqAntennaSuitability
    health_before = $healthBefore
    health_after_capture = $healthAfterCapture
    driver_after_retrieval = $driverAfterRetrieval
    health_after_retrieval = $healthAfterRetrieval
    capture_done_line = $doneLine
    orcsdr_commit = (& git -C (Join-Path $PSScriptRoot '..\..\..') rev-parse HEAD).Trim()
  }
  $metadataPath = [IO.Path]::ChangeExtension($output, '.json')
  $metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $metadataPath -Encoding utf8
  & python (Join-Path $PSScriptRoot 'analyze_rtl_iq.py') $output --rate $start.Rate --csv
  if ($LASTEXITCODE -ne 0) { throw "IQ analyzer failed with exit code $LASTEXITCODE" }
  [void](Send-And-Wait "RTL_TUNE $IqBand $IqFrequency" '^RTL_TUNE_OK ' 20)
  Write-SoakLine "RTL_IQ_DIAGNOSTIC_RESULT pass=1 transition=$($start.Transition) bytes=$totalBytes sha256=$localSha capture=\"$output\" metadata=\"$metadataPath\""
}

function Invoke-GainSweepTest {
  Wait-DeviceReady 60 11000
  Connect-Authenticated
  $initial = $null
  $initialSignal = $null
  $initialDriver = $null
  $initialSoftwareAuto = $false
  $initialVerbosity = $null
  try {
    $verbosity = Send-And-Wait 'RTL_SERIAL VERBOSITY' '^RTL_SERIAL_VERBOSITY mode=(QUIET|NORMAL|DEBUG|TRACE)$'
    $initialVerbosity = $verbosity.Split('=')[-1]
    [void](Send-And-Wait 'RTL_SERIAL VERBOSITY QUIET' '^RTL_SERIAL_VERBOSITY_OK mode=QUIET$')
    $initial = Get-UiState
    $initialSignal = Get-SignalStatus
    $initialDriver = Get-DriverStatus
    if ($initial.Band -in @('FM', 'AM')) {
      $initialSoftwareAuto = (Send-And-Wait "RTL_$($initial.Band)_GAIN STATUS" "^RTL_$($initial.Band)_GAIN_STATUS ").Contains('mode=AUTO')
    }
    if ($GainSweepBand -eq 'FM') {
      if ($GainSweepFrequency -lt 76000000 -or $GainSweepFrequency -gt 108000000) {
        throw '-GainSweepBand FM requires -GainSweepFrequency between 76000000 and 108000000.'
      }
      [void](Open-Ui 'FM' 'FM')
      [void](Send-And-Wait "RTL_UI ACTION FM TUNE $GainSweepFrequency" '^RTL_UI_ACTION_OK$')
    } else {
      [void](Send-And-Wait "RTL_TUNE $GainSweepBand $GainSweepFrequency" '^RTL_TUNE_OK ')
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
      $driver = Get-DriverStatus
      $signal = Get-SignalStatus
      if ($driver.State -eq 'STREAMING' -and $driver.Bytes -gt 0 -and
          $signal.Band -eq $GainSweepBand -and $signal.Frequency -eq $GainSweepFrequency) { break }
      Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($driver.State -ne 'STREAMING' -or $driver.Bytes -eq 0 -or $driver.Profile -ne 2 -or $driver.GainCap -ne 1 -or
        $signal.Band -ne $GainSweepBand -or $signal.Frequency -ne $GainSweepFrequency) {
      throw "V3 gain sweep requires the requested band streaming with manual gain; state=$($driver.State) band=$($signal.Band) frequency=$($signal.Frequency) profile=$($driver.Profile) gain_cap=$($driver.GainCap) bytes=$($driver.Bytes)"
    }

    if ($GainSweepBand -eq 'FM') {
      [void](Send-And-Wait 'RTL_UI ACTION FM GAIN_AUTO' '^RTL_UI_ACTION_OK$')
      Start-Sleep -Milliseconds 300
      $auto = Send-And-Wait 'RTL_FM_GAIN STATUS' '^RTL_FM_GAIN_STATUS '
      if ($auto -notmatch 'mode=AUTO selecting=[01] gain_tenth_db=\d+ target_dbfs=-24\.0') {
        throw "FM bounded auto gain failed: $auto"
      }
    }

    $gains = if ($GainSweepGains.Count) { $GainSweepGains } else {
      @(0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496)
    }
    foreach ($gain in $gains) {
      $command = if ($GainSweepBand -eq 'FM') { "RTL_UI ACTION FM GAIN $gain" } else { "RTL_DRIVER GAIN $gain" }
      $pattern = if ($GainSweepBand -eq 'FM') { '^RTL_UI_ACTION_OK$' } else { '^RTL_DRIVER_RESULT .*accepted=1 result=ESP_OK$' }
      [void](Send-And-Wait $command $pattern)
      $deadline = [DateTime]::UtcNow.AddSeconds(5)
      do {
        $driver = Get-DriverStatus
        if ($driver.State -eq 'STREAMING' -and $driver.Bytes -gt 0 -and
            $driver.Mode -eq 'MANUAL' -and $driver.Gain -eq $gain) { break }
        Start-Sleep -Milliseconds 100
      } while ([DateTime]::UtcNow -lt $deadline)
      $maxSignalTenths = -900
      $sampleDeadline = [DateTime]::UtcNow.AddMilliseconds($GainSweepDwellMs)
      do {
        $signal = Get-SignalStatus
        $maxSignalTenths = [Math]::Max($maxSignalTenths, $signal.SignalTenths)
        Start-Sleep -Milliseconds 100
      } while ([DateTime]::UtcNow -lt $sampleDeadline)
      $driver = Get-DriverStatus
      if ($driver.State -ne 'STREAMING' -or $driver.Mode -ne 'MANUAL' -or $driver.Gain -ne $gain -or $driver.Bytes -eq 0 -or
          $signal.Band -ne $GainSweepBand -or $signal.Frequency -ne $GainSweepFrequency) {
        throw "Gain sweep sample failed: requested=$gain state=$($driver.State) mode=$($driver.Mode) gain=$($driver.Gain) bytes=$($driver.Bytes) signal=$($signal.Line)"
      }
      Write-SoakLine "RTL_GAIN_SWEEP_SAMPLE band=$GainSweepBand frequency_hz=$GainSweepFrequency gain_tenth_db=$gain signal_dbfs_tenths=$($signal.SignalTenths) max_signal_dbfs_tenths=$maxSignalTenths bytes=$($driver.Bytes)"
    }
    Assert-Health
    Write-SoakLine "RTL_GAIN_SWEEP_RESULT pass=1 profile=2 band=$GainSweepBand frequency_hz=$GainSweepFrequency samples=$($gains.Count)"
  } finally {
    try {
      if ($null -ne $initial -and $null -ne $initialSignal -and
          $initial.Band -in @('FM','AM','WX','CB','LORA','BROWSE','ADSB','P25')) {
        [void](Send-And-Wait "RTL_TUNE $($initial.Band) $($initialSignal.Frequency)" '^RTL_TUNE_(?:OK|UNAVAILABLE|INVALID)')
      }
      if ($null -ne $initialDriver) {
        if ($initialSoftwareAuto) {
          [void](Send-And-Wait "RTL_UI ACTION $($initial.Band) GAIN_AUTO" '^RTL_UI_ACTION_OK$')
        } elseif ($initialDriver.Mode -eq 'AUTO' -and $initialDriver.GainAutoCap -eq 1) {
          [void](Send-And-Wait 'RTL_DRIVER GAINMODE AUTO' '^RTL_DRIVER_RESULT .*accepted=1 result=ESP_OK$')
        } else {
          [void](Send-And-Wait "RTL_DRIVER GAIN $($initialDriver.Gain)" '^RTL_DRIVER_RESULT .*accepted=1 result=ESP_OK$')
        }
      }
      if ($null -ne $initial) {
        [void](Send-And-Wait "RTL_UI OPEN $($initial.Screen)" '^RTL_UI_OPEN_(?:OK|INVALID)')
      }
      if ($null -ne $initialVerbosity) {
        [void](Send-And-Wait "RTL_SERIAL VERBOSITY $initialVerbosity" "^RTL_SERIAL_VERBOSITY_OK mode=$initialVerbosity$")
      }
    } catch {
      Write-Warning "Could not restore initial gain sweep state: $($_.Exception.Message)"
    }
  }
}

function Set-AmFilter([uint32]$TargetHz) {
  [void](Send-And-Wait "RTL_UI ACTION AM FILTER $TargetHz" '^RTL_UI_ACTION_OK$')
  Start-Sleep -Milliseconds 150
  $signal = Get-SignalStatus
  if ($signal.Band -eq 'AM' -and $signal.FilterHz -eq $TargetHz) { return $signal }
  throw "AM filter did not reach $TargetHz Hz: $($signal.Line)"
}

function Get-AmScanStatus {
  $line = Send-And-Wait 'RTL_AM_SCAN STATUS' '^RTL_AM_SCAN_STATUS '
  if ($line -notmatch '^RTL_AM_SCAN_STATUS active=([01]) step=(\d+) total=(\d+) found=(\d+) frequency_hz=(\d+) prompt=([01])$') {
    throw "Malformed AM scan status: $line"
  }
  [pscustomobject]@{
    Active = [int]$Matches[1]; Step = [int]$Matches[2]; Total = [int]$Matches[3]
    Found = [int]$Matches[4]; Frequency = [uint32]$Matches[5]; Prompt = [int]$Matches[6]
  }
}

function Invoke-AmBroadcastTest {
  Wait-DeviceReady 60 11000
  Connect-Authenticated
  $initial = $null
  $initialSignal = $null
  $initialDriver = $null
  $initialAmGainAuto = $null
  $initialVerbosity = $null
  $soundWasEnabled = $null
  try {
    $verbosity = Send-And-Wait 'RTL_SERIAL VERBOSITY' '^RTL_SERIAL_VERBOSITY mode=(QUIET|NORMAL|DEBUG|TRACE)$'
    $initialVerbosity = $verbosity.Split('=')[-1]
    [void](Send-And-Wait 'RTL_SERIAL VERBOSITY QUIET' '^RTL_SERIAL_VERBOSITY_OK mode=QUIET$')
    $soundWasEnabled = (Send-And-Wait 'RTL_SOUND' '^RTL_SOUND_STATUS enabled=[01]$').EndsWith('1')
    if (-not $soundWasEnabled) { [void](Send-And-Wait 'RTL_SOUND ON' '^RTL_SOUND_OK enabled=1$') }
    Drain-SerialOutput
    $initial = Get-UiState
    $initialSignal = Get-SignalStatus
    [void](Open-Ui 'AM' 'AM')
    $routeDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
      $audio = Get-AudioStatus
      if ($audio.headphone_connected -ne 1 -or $audio.internal_speaker_muted -eq 1) { break }
      Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $routeDeadline)
    if ($audio.headphone_connected -eq 1 -and $audio.internal_speaker_muted -ne 1) {
      throw 'Headphones detected but internal speaker is not muted.'
    }
    $streamDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
      $driver = Get-DriverStatus
      if ($driver.State -eq 'STREAMING' -and $driver.Bytes -gt 0) { break }
      Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $streamDeadline)
    $initialDriver = $driver
    $initialAmGainAuto = (Send-And-Wait 'RTL_AM_GAIN STATUS' '^RTL_AM_GAIN_STATUS mode=(AUTO|MANUAL) ').Contains('mode=AUTO')
    if ($driver.State -ne 'STREAMING' -or $driver.Bytes -eq 0) {
      throw "AM test requires active IQ streaming; state=$($driver.State) bytes=$($driver.Bytes)"
    }
    $lastBytes = $driver.Bytes
    foreach ($frequency in @(590000, 1120000, 1280000)) {
      [void](Send-And-Wait "RTL_UI ACTION AM TUNE $frequency" '^RTL_UI_ACTION_OK$')
      foreach ($filter in @(3000, 6000, 10000, 30000)) {
        [void](Set-AmFilter $filter)
        Start-Sleep -Seconds $DwellSeconds
        $signal = Get-SignalStatus
        if ($signal.Band -ne 'AM' -or $signal.Frequency -ne $frequency -or
            $signal.FilterHz -ne $filter) {
          throw "AM state mismatch: $($signal.Line)"
        }
        $driver = Get-DriverStatus
        if ($driver.State -ne 'STREAMING' -or $driver.Bytes -le $lastBytes -or
            $driver.EffectiveSps -eq 0) {
          throw "AM IQ did not progress at frequency=$frequency filter=$filter state=$($driver.State) bytes=$($driver.Bytes) effective_sps=$($driver.EffectiveSps)"
        }
        $lastBytes = $driver.Bytes
        Write-SoakLine "RTL_AM_REGRESSION_SAMPLE pass=1 frequency_hz=$frequency filter_hz=$filter signal_dbfs_tenths=$($signal.SignalTenths) bytes=$($driver.Bytes) effective_sps=$($driver.EffectiveSps) overruns=$($driver.Overruns) drops=$($driver.Drops)"
      }
      Assert-Health
    }
    [void](Open-Ui 'HOME' 'AM')
    [void](Open-Ui 'AM' 'AM')
    $signal = Get-SignalStatus
    if ($signal.Frequency -ne 1280000) {
      throw "AM dashboard entry changed the current station: $($signal.Line)"
    }
    Write-SoakLine 'RTL_AM_ENTRY_REGRESSION pass=1 preserved_frequency_hz=1280000'
    [void](Send-And-Wait 'RTL_UI ACTION AM GAIN_AUTO' '^RTL_UI_ACTION_OK$')
    Start-Sleep -Milliseconds 300
    $driver = Get-DriverStatus
    $auto = Send-And-Wait 'RTL_AM_GAIN STATUS' '^RTL_AM_GAIN_STATUS '
    if ($driver.Mode -ne 'MANUAL' -or
        $auto -notmatch 'mode=AUTO selecting=1 gain_tenth_db=0 target_dbfs=-24\.0') {
      throw "AM bounded auto gain failed: driver_mode=$($driver.Mode) status=$auto"
    }
    foreach ($gain in @(0, 496)) {
      [void](Send-And-Wait "RTL_UI ACTION AM GAIN $gain" '^RTL_UI_ACTION_OK$')
      Start-Sleep -Milliseconds 300
      $driver = Get-DriverStatus
      if ($driver.Mode -ne 'MANUAL' -or $driver.Gain -ne $gain) {
        throw "AM manual gain action failed: requested=$gain mode=$($driver.Mode) gain=$($driver.Gain)"
      }
    }
    Write-SoakLine 'RTL_AM_GAIN_REGRESSION pass=1 auto=lowest_usable manual_range_tenth_db=0-496'
    [void](Open-Ui 'HOME' 'AM')
    [void](Open-Ui 'FM' 'FM')
    $fmDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
      $fmDriver = Get-DriverStatus
      if ($fmDriver.State -eq 'STREAMING' -and $fmDriver.Bytes -gt 0) { break }
      Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $fmDeadline)
    if ($fmDriver.State -ne 'STREAMING' -or $fmDriver.Bytes -eq 0 -or $fmDriver.Mode -ne 'MANUAL') {
      throw "AM to FM transition did not restore FM tuner state: state=$($fmDriver.State) bytes=$($fmDriver.Bytes) mode=$($fmDriver.Mode)"
    }
    [void](Send-And-Wait 'RTL_UI ACTION FM GAIN_AUTO' '^RTL_UI_ACTION_OK$')
    Start-Sleep -Milliseconds 300
    $fmAuto = Send-And-Wait 'RTL_FM_GAIN STATUS' '^RTL_FM_GAIN_STATUS '
    if ($fmAuto -notmatch 'mode=AUTO selecting=1 gain_tenth_db=0 target_dbfs=-24\.0') {
      throw "FM bounded auto gain failed: $fmAuto"
    }
    foreach ($gain in @(0, 496)) {
      [void](Send-And-Wait "RTL_UI ACTION FM GAIN $gain" '^RTL_UI_ACTION_OK$')
      Start-Sleep -Milliseconds 300
      $fmDriver = Get-DriverStatus
      if ($fmDriver.Mode -ne 'MANUAL' -or $fmDriver.Gain -ne $gain) {
        throw "FM manual gain action failed: requested=$gain mode=$($fmDriver.Mode) gain=$($fmDriver.Gain)"
      }
    }
    Write-SoakLine 'RTL_FM_GAIN_REGRESSION pass=1 auto=lowest_usable manual_range_tenth_db=0-496'
    [void](Open-Ui 'AM' 'AM')
    Write-SoakLine 'RTL_AM_FM_TRANSITION_REGRESSION pass=1 software_auto_gain_mode=MANUAL'
    $audioBeforeScan = Get-AudioStatus
    [void](Send-And-Wait 'RTL_UI ACTION AM SCAN' '^RTL_UI_ACTION_OK$')
    $scanDeadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
      $scan = Get-AmScanStatus
      if ($scan.Active -eq 1) { break }
      Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $scanDeadline)
    if ($scan.Active -ne 1 -or $scan.Total -lt 100) {
      throw "AM scan did not start: $($scan | ConvertTo-Json -Compress)"
    }
    $streamDeadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
      $driver = Get-DriverStatus
      if ($driver.State -eq 'STREAMING' -and
          $driver.EffectiveSps -gt 0 -and $driver.EffectiveSps -lt 2200000) { break }
      Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $streamDeadline)
    if ($driver.State -ne 'STREAMING' -or
        $driver.EffectiveSps -eq 0 -or $driver.EffectiveSps -ge 2200000) {
      throw "AM scan did not preserve the normal stream: state=$($driver.State) effective_sps=$($driver.EffectiveSps)"
    }
    $scanDeadline = [DateTime]::UtcNow.AddSeconds(60)
    do {
      $scan = Get-AmScanStatus
      if ($scan.Active -eq 0) { break }
      Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $scanDeadline)
    if ($scan.Active -ne 0 -or $scan.Step -ne $scan.Total -or $scan.Found -gt 6 -or
        $scan.Prompt -ne 1) {
      throw "AM scan did not finish with a valid candidate count: $($scan | ConvertTo-Json -Compress)"
    }
    [void](Send-And-Wait 'RTL_UI ACTION AM SCAN_DISCARD' '^RTL_UI_ACTION_OK$')
    $scan = Get-AmScanStatus
    if ($scan.Prompt -ne 0) { throw 'AM scan result popup did not close after DISCARD.' }
    $restoreDeadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
      $restoredDriver = Get-DriverStatus
      $restoredSignal = Get-SignalStatus
      $restoredAudio = Get-AudioStatus
      if ($restoredDriver.State -eq 'STREAMING' -and
          $restoredDriver.EffectiveSps -gt 0 -and $restoredDriver.EffectiveSps -lt 2200000 -and
          $restoredSignal.Band -eq 'AM' -and $restoredSignal.Frequency -eq 1280000 -and
          $restoredAudio.speaker_running -eq 1 -and
          [uint64]$restoredAudio.audio_chunks -gt [uint64]$audioBeforeScan.audio_chunks) { break }
      Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $restoreDeadline)
    if ($restoredDriver.State -ne 'STREAMING' -or
        $restoredDriver.EffectiveSps -eq 0 -or $restoredDriver.EffectiveSps -ge 2200000 -or
        $restoredSignal.Band -ne 'AM' -or $restoredSignal.Frequency -ne 1280000 -or
        $restoredAudio.speaker_running -ne 1 -or
        [uint64]$restoredAudio.audio_chunks -le [uint64]$audioBeforeScan.audio_chunks) {
      throw "AM radio did not recover after scan: driver=$($restoredDriver | ConvertTo-Json -Compress) signal=$($restoredSignal.Line) audio=$($restoredAudio | ConvertTo-Json -Compress)"
    }
    Write-SoakLine "RTL_AM_SCAN_REGRESSION pass=1 action=retune+populate channels=$($scan.Total) found=$($scan.Found) capacity=6 effective_sps=$($driver.EffectiveSps)"
    Write-SoakLine "RTL_AM_SCAN_RESTORE pass=1 popup=discard band=AM frequency_hz=1280000 effective_sps=$($restoredDriver.EffectiveSps) audio_chunks=$($restoredAudio.audio_chunks)"
    Write-SoakLine 'RTL_AM_REGRESSION_RESULT pass=1 frequencies=3 filters=4 samples=12'
  } finally {
    try {
      if ($null -ne $initial -and
          $initial.Band -in @('FM','AM','WX','CB','LORA','BROWSE','ADSB','P25')) {
        [void](Send-And-Wait "RTL_TUNE $($initial.Band) $($initial.Frequency)" '^RTL_TUNE_(?:OK|UNAVAILABLE|INVALID)')
      }
      if ($null -ne $initial -and $null -ne $initialSignal -and
          $initial.Band -eq 'AM' -and $initialSignal.FilterHz -ge 3000 -and
          $initialSignal.FilterHz -le 30000) {
        [void](Open-Ui 'AM' 'AM')
        [void](Set-AmFilter $initialSignal.FilterHz)
      }
      if ($null -ne $initialDriver -and $null -ne $initialAmGainAuto) {
        if ($initialAmGainAuto) {
          [void](Send-And-Wait 'RTL_UI ACTION AM GAIN_AUTO' '^RTL_UI_ACTION_OK$')
        } else {
          [void](Send-And-Wait "RTL_UI ACTION AM GAIN $($initialDriver.Gain)" '^RTL_UI_ACTION_OK$')
        }
      }
      if ($null -ne $initial) {
        [void](Send-And-Wait "RTL_UI OPEN $($initial.Screen)" '^RTL_UI_OPEN_(?:OK|INVALID)')
      }
      if ($null -ne $initialVerbosity) {
        [void](Send-And-Wait "RTL_SERIAL VERBOSITY $initialVerbosity" "^RTL_SERIAL_VERBOSITY_OK mode=$initialVerbosity$")
      }
      if ($soundWasEnabled -eq $false) {
        [void](Send-And-Wait 'RTL_SOUND OFF' '^RTL_SOUND_OK enabled=0$')
      }
    } catch {
      Write-Warning "Could not restore initial AM test state: $($_.Exception.Message)"
    }
  }
}

function Invoke-Driver080Rc3Test {
  Wait-DeviceReady
  Connect-Authenticated
  $selfCheck = Send-And-Wait 'RTL_DRIVER SELF_CHECK' '^RTL_DRIVER_SELF_CHECK '
  if ($selfCheck -notmatch 'pass=1 version=0\.8\.0-rc3 profile=(1|2) ') { throw "Driver self-check failed: $selfCheck" }
  $deadline = [DateTime]::UtcNow.AddSeconds(30)
  do {
    $initial = Get-DriverStatus
    if ($initial.State -eq 'STREAMING' -and $initial.Bytes -gt 0) { break }
    Start-Sleep -Milliseconds 500
  } while ([DateTime]::UtcNow -lt $deadline)
  if ($initial.State -ne 'STREAMING' -or $initial.Bytes -eq 0) {
    throw "Driver test requires active IQ streaming; state=$($initial.State) bytes=$($initial.Bytes)"
  }
  $isV4 = $initial.Profile -eq 1
  $isV3 = $initial.Profile -eq 2
  $hasDirectSampling = ($initial.DeviceCaps -band 0x40) -ne 0
  $hasFrequencyCorrection = ($initial.DeviceCaps -band 0x100) -ne 0
  if ((!$isV4 -and !$isV3) -or !$hasFrequencyCorrection -or
      $initial.ShadowOk -ne 1 -or $initial.MetricsOk -ne 1 -or $initial.FrequencyOk -ne 1 -or
      ($isV4 -and ($initial.Provisional -ne 0 -or $initial.GainAutoCap -ne 1 -or
                   $initial.RtlAgcCap -ne 1 -or $initial.GainCap -ne 1 -or
                   $initial.BiasCap -ne 1)) -or
      ($isV3 -and ($initial.Provisional -ne 1 -or !$hasDirectSampling -or
                   $initial.GainCap -ne 1 -or $initial.GainAutoCap -ne 0 -or
                   $initial.RtlAgcCap -ne 0 -or $initial.BiasCap -ne 0))) {
    throw 'Required Blog V4/V3c v0.8.0-rc3 profile, capabilities, or status getter is unavailable.'
  }

  $last = $initial
  function Test-Transition([string]$Command, [string]$Mode, [int]$Gain, [int]$RtlAgc) {
    $reply = Send-And-Wait $Command '^RTL_DRIVER_RESULT '
    if ($reply -notmatch 'accepted=1 result=ESP_OK') { throw "Driver request rejected: $reply" }
    Start-Sleep -Seconds 2
    $next = Get-DriverStatus
    if ($next.Mode -ne $Mode -or ($Gain -ge 0 -and $next.Gain -ne $Gain) -or
        ($RtlAgc -ge 0 -and $next.RtlAgc -ne $RtlAgc)) {
      throw "Shadow mismatch after $Command"
    }
    if ($next.Bytes -le $script:last.Bytes) { throw "IQ stopped after $Command" }
    if ($next.Overruns -gt $initial.Overruns + 16 -or $next.Drops -gt $initial.Drops + 16) {
      throw "Drop counters grew excessively after $Command"
    }
    Write-SoakLine "RTL_DRIVER_080_RC2_STEP command=$($Command.Replace(' ', '_')) pass=1 bytes=$($next.Bytes) effective_sps=$($next.EffectiveSps) overruns=$($next.Overruns) drops=$($next.Drops)"
    $script:last = $next
  }

  function Test-Frequency([uint32]$Frequency, [string]$Route) {
    $reply = Send-And-Wait "RTL_DRIVER TUNE $Frequency" '^RTL_DRIVER_RESULT '
    if ($reply -notmatch 'accepted=1 result=ESP_OK') { throw "Driver tune rejected: $reply" }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
      Start-Sleep -Milliseconds 250
      $next = Get-DriverStatus
      if ($next.Frequency -eq $Frequency -and $next.FrequencyOk -eq 1 -and
          $next.Route -eq $Route -and $next.Bytes -gt $script:last.Bytes) { break }
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($next.Frequency -ne $Frequency -or $next.FrequencyOk -ne 1 -or $next.Route -ne $Route) {
      throw "Exact-frequency mismatch after $Frequency Hz: $($next | ConvertTo-Json -Compress)"
    }
    if ($next.Bytes -le $script:last.Bytes) { throw "IQ stopped after $Frequency Hz" }
    if ($next.Overruns -gt $script:last.Overruns + 16 -or $next.Drops -gt $script:last.Drops + 16) {
      throw "Drop counters grew excessively after $Frequency Hz"
    }
    Write-SoakLine "RTL_DRIVER_080_RC2_FREQ pass=1 requested_hz=$Frequency reported_hz=$($next.Frequency) route=$($next.Route) bytes=$($next.Bytes) effective_sps=$($next.EffectiveSps) overruns=$($next.Overruns) drops=$($next.Drops)"
    $script:last = $next
  }

  $script:last = $last
  try {
    foreach ($frequency in @(24000, 1000000, 10000000, 23999999, 24000000, 24000001,
                              28799999, 28800000, 28800001, 96100000, 162400000,
                              433000000, 1090000000, 1766000000)) {
      $route = if ($isV3 -and $frequency -lt 24000000) { 'DIRECT_Q' }
               elseif ($isV4 -and $frequency -lt 28800000) { 'HF_UPCONVERTER' }
               else { 'TUNER' }
      Test-Frequency $frequency $route
    }
    if ($isV4) {
      Test-Transition 'RTL_DRIVER GAINMODE MANUAL' 'MANUAL' -1 -1
      Test-Transition 'RTL_DRIVER GAIN 297' 'MANUAL' 297 -1
      Test-Transition 'RTL_DRIVER GAINMODE AUTO' 'AUTO' 297 -1
      Test-Transition 'RTL_DRIVER RTLAGC ON' 'AUTO' 297 1
      Test-Transition 'RTL_DRIVER RTLAGC OFF' 'AUTO' 297 0
      if ($TestBiasTee) {
        $target = 1 - $initial.Bias
        Test-Transition "RTL_DRIVER BIAS $(if ($target) { 'ON' } else { 'OFF' })" 'AUTO' 297 0
      }
    }
    Write-SoakLine "RTL_DRIVER_080_RC2_RESULT pass=1 version=$($initial.Version) profile=$($initial.ProfileName) bias_tested=$([int]($isV4 -and [bool]$TestBiasTee)) evidence=capabilities+exact_frequency+route+iq_continuity"
  } finally {
    try {
      [void](Send-And-Wait "RTL_DRIVER TUNE $($initial.Frequency)" '^RTL_DRIVER_RESULT ')
      if ($isV4) {
        [void](Send-And-Wait "RTL_DRIVER GAIN $($initial.Gain)" '^RTL_DRIVER_RESULT ')
        [void](Send-And-Wait "RTL_DRIVER GAINMODE $($initial.Mode)" '^RTL_DRIVER_RESULT ')
        [void](Send-And-Wait "RTL_DRIVER RTLAGC $(if ($initial.RtlAgc) { 'ON' } else { 'OFF' })" '^RTL_DRIVER_RESULT ')
        if ($TestBiasTee) {
          [void](Send-And-Wait "RTL_DRIVER BIAS $(if ($initial.Bias) { 'ON' } else { 'OFF' })" '^RTL_DRIVER_RESULT ')
        }
      }
    } catch { Write-Warning "Could not restore driver shadow state: $($_.Exception.Message)" }
  }
}

function Invoke-RadioScanTest {
  Wait-DeviceReady 60 11000
  Connect-Authenticated
  $rtlDeadline = [DateTime]::UtcNow.AddSeconds(30)
  do {
    $rtlStatus = Send-And-Wait 'RTL_STATUS' '^RTL_SDR_STATUS ' 20
    if ($rtlStatus -match 'connected=true ') { break }
    Start-Sleep -Milliseconds 500
  } while ([DateTime]::UtcNow -lt $rtlDeadline)
  if ($rtlStatus -notmatch 'connected=true ') { throw "RTL-SDR did not enumerate: $rtlStatus" }

  $initial = Get-UiState
  $baseline = $null
  $previousUptime = [uint64]0
  try {
    for ($cycle = 0; $cycle -le $Cycles; $cycle++) {
      [void](Open-Ui 'FM' 'FM')
      [void](Send-And-Wait 'RTL_PRESET_SCAN' '^RTL_PRESET_SCAN_QUEUED$')
      [void](Read-MatchingLine '^RTL_PRESET_SCAN start$' 20)

      [void](Open-Ui 'P25' 'P25')
      $p25 = Get-RadioFrequency
      if ($p25.Band -ne 'P25') { throw "FM scan restored stale state after takeover: band=$($p25.Band)" }

      $script:serial.WriteLine('RTL_P25_SCAN')
      [void](Read-MatchingLine '^RTL_P25_SURVEY start candidates=\d+ dwell_ms=1500$' 30)
      [void](Open-Ui 'LORA' 'LORA')
      Start-Sleep -Seconds $DwellSeconds
      $lora = Get-RadioFrequency
      if ($lora.Band -ne 'LORA') { throw "P25 survey restored stale state after takeover: band=$($lora.Band)" }

      $health = Get-HealthStatus
      Assert-HealthStatus $health
      if ($previousUptime -ne 0 -and !(Test-UptimeAdvanced $previousUptime $health.UptimeMs)) {
        throw "Device uptime did not advance; reset suspected: $($health.Line)"
      }
      $previousUptime = $health.UptimeMs
      if ($cycle -eq 0) {
        $baseline = $health
        Write-SoakLine "RTL_RADIO_SCAN_WARMUP pass=1 free_heap=$($health.FreeHeap) dma_free=$($health.DmaFree) dma_largest=$($health.DmaLargest) stack_hwm=$($health.MainStackHwm)"
      } else {
        $heapDelta = [int64]$health.FreeHeap - [int64]$baseline.FreeHeap
        $dmaDelta = [int64]$health.DmaFree - [int64]$baseline.DmaFree
        if ($heapDelta -lt -4096 -or $dmaDelta -lt -2048 -or $health.DmaLargest -lt 20480) {
          throw "Radio scan memory regression: heap_delta=$heapDelta dma_delta=$dmaDelta dma_largest=$($health.DmaLargest)"
        }
        Write-SoakLine "RTL_RADIO_SCAN_CYCLE cycle=$cycle pass=1 heap_delta=$heapDelta dma_delta=$dmaDelta free_heap=$($health.FreeHeap) dma_largest=$($health.DmaLargest) stack_hwm=$($health.MainStackHwm)"
      }
    }
    Write-SoakLine "RTL_RADIO_SCAN_RESULT pass=1 cycles=$Cycles lines=$script:linesSeen"
  } finally {
    try {
      if ($initial.Band -in @('FM','AM','WX','CB','LORA','BROWSE','ADSB','P25')) {
        [void](Send-And-Wait "RTL_TUNE $($initial.Band) $($initial.Frequency)" '^RTL_TUNE_(?:OK|UNAVAILABLE|INVALID)')
      }
      [void](Send-And-Wait "RTL_UI OPEN $($initial.Screen)" '^RTL_UI_OPEN_(?:OK|INVALID)')
    } catch {
      Write-Warning "Could not restore initial device state: $($_.Exception.Message)"
    }
  }
}

$script:serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$script:serial.NewLine = "`n"
$script:serial.ReadTimeout = 250
$script:serial.WriteTimeout = 2000
$script:serial.DtrEnable = $false
$script:serial.RtsEnable = $false

try {
  $script:serial.Open()
  Start-Sleep -Milliseconds 250
  $script:serial.DiscardInBuffer()

  if ($ResetDevice) { Reset-DeviceBaseline }

  if ($SdSelfCheck) {
    Wait-DeviceReady 60 11000
    Connect-Authenticated
    $result = Send-And-Wait 'RTL_SD_SELF_CHECK' '^RTL_SD_SELF_CHECK_RESULT ' 30
    if ($result -notmatch ' pass=1$') { throw "SD self-check failed: $result" }
    exit 0
  }

  if ($SdBenchmark) {
    Wait-DeviceReady 60 11000
    Connect-Authenticated
    Invoke-SdBenchmark
    exit 0
  }

  if ($Driver080Rc3) { Invoke-Driver080Rc3Test; exit 0 }
  if ($WifiOnly) {
    Wait-DeviceReady 60 11000
    $initialUi = Get-UiState
    Connect-Authenticated
    Assert-WifiCli $initialUi
    exit 0
  }
  if ($WifiCoexistence) {
    Wait-DeviceReady 60 11000
    $initialUi = Get-UiState
    Connect-Authenticated
    Assert-WifiCoexistence $initialUi
    exit 0
  }
  if ($WifiCoexistenceDiagnostic) {
    Wait-DeviceReady 60 11000
    $initialUi = Get-UiState
    Connect-Authenticated
    Assert-WifiCoexistenceDiagnostic $initialUi
    exit 0
  }
  if ($DataOnly) {
    Wait-DeviceReady 60 11000
    Connect-Authenticated
    Assert-DataServices
    exit 0
  }
  if ($IqDiagnostic) { Invoke-IqDiagnosticCapture; exit 0 }
  if ($C6Update) { Invoke-C6UpdateTest; exit 0 }
  if ($RadioScan) { Invoke-RadioScanTest; exit 0 }
  if ($AmBroadcast) { Invoke-AmBroadcastTest; exit 0 }
  if ($GainSweep) { Invoke-GainSweepTest; exit 0 }

  if ($Profile) {
    $commit = (& git -C (Join-Path $PSScriptRoot '..\..\..') rev-parse --short HEAD 2>$null)
    Write-SoakLine "RTL_UI_SOAK_BEGIN profile=$Profile cycles=$Cycles seed=$Seed port=$Port commit=$commit"
    Write-SoakLine "RTL_UI_SOAK_LOG path=$($script:soakLogPath)"
  }

  if (-not $Soak) {
    if ($Run) {
      Wait-DeviceReady 60 11000
      Connect-Authenticated
    }
    $command = if ($Run) { 'RTL_UI_REGRESSION RUN' } else { 'RTL_UI_REGRESSION CHECK' }
    $line = Send-And-Wait $command '^RTL_UI_REGRESSION_RESULT '
    if ($line -notmatch ' pass=1 ') { throw "UI regression failed: $line" }
    exit 0
  }

  Wait-DeviceReady
  Connect-Authenticated
  $rtlDeadline = [DateTime]::UtcNow.AddSeconds(30)
  do {
    $rtlStatus = Send-And-Wait 'RTL_STATUS' '^RTL_SDR_STATUS ' 20
    if ($rtlStatus -match 'connected=true ') { break }
    Start-Sleep -Milliseconds 500
  } while ([DateTime]::UtcNow -lt $rtlDeadline)
  if ($rtlStatus -notmatch 'connected=true ') { throw "RTL-SDR did not enumerate: $rtlStatus" }
  $initial = Get-UiState
  $initialVerbosity = $null
  $soundWasEnabled = $null
  try {
    $verbosity = Send-And-Wait 'RTL_SERIAL VERBOSITY' '^RTL_SERIAL_VERBOSITY mode=(QUIET|NORMAL|DEBUG|TRACE)$'
    $initialVerbosity = $verbosity.Split('=')[-1]
    [void](Send-And-Wait 'RTL_SERIAL VERBOSITY QUIET' '^RTL_SERIAL_VERBOSITY_OK mode=QUIET$')
    $sound = Send-And-Wait 'RTL_SOUND' '^RTL_SOUND_STATUS enabled=[01]$'
    $soundWasEnabled = $sound.EndsWith('1')
    if (-not $soundWasEnabled) { [void](Send-And-Wait 'RTL_SOUND ON' '^RTL_SOUND_OK enabled=1$') }
    $random = [Random]::new($Seed)

    [void](Open-Ui 'FM' 'FM')
    Assert-FmAudioProgress
    for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
      $targets = @(
        [pscustomobject]@{ Screen = 'ADSB'; Band = 'ADSB' },
        [pscustomobject]@{ Screen = 'LORA'; Band = 'LORA' },
        [pscustomobject]@{ Screen = 'P25'; Band = 'P25' }
      )
      if ($Profile -in @('Stress', 'Overnight')) { $targets = $targets | Sort-Object { $random.Next() } }
      foreach ($target in $targets) {
        [void](Open-Ui $target.Screen $target.Band)
        Watch-Responsive $DwellSeconds $target.Screen $target.Band
        [void](Open-Ui 'HOME' $target.Band)
        Watch-Responsive $DwellSeconds 'HOME' $target.Band
      }
      $graphicsBeforeSettings = (Get-UiState).Graphics
      [void](Open-Ui 'SETTINGS' $targets[-1].Band)
      Watch-Responsive $DwellSeconds 'SETTINGS' $targets[-1].Band
      $homeAfterSettings = Open-Ui 'HOME' $targets[-1].Band
      if ($homeAfterSettings.Graphics -ne $graphicsBeforeSettings) {
        throw "Settings navigation did not restore graphics: before=$graphicsBeforeSettings after=$($homeAfterSettings.Graphics)"
      }
      [void](Open-Ui 'RF_LAB' $targets[-1].Band)
      [void](Send-And-Wait 'RTL_LAB SELF_CHECK' '^RTL_LAB_SELF_CHECK pass=1$')
      [void](Send-And-Wait 'RTL_LAB PAGE CONTROLS' '^RTL_LAB_OK page=CONTROLS$')
      Watch-Responsive $DwellSeconds 'RF_LAB' $targets[-1].Band
      [void](Send-And-Wait 'RTL_LAB CLOSE' '^RTL_LAB_OK close=queued$')
      [void](Wait-UiState 'HOME' $targets[-1].Band)
      [void](Open-Ui 'FM' 'FM')
      Assert-FmAudioProgress
      if ($cycle -eq 1 -or $cycle % 5 -eq 0) { Assert-SoundCycle }
      if ($Profile -in @('Stress', 'Overnight') -and $cycle % $WifiEvery -eq 0) {
        Assert-WifiCycle
        [void](Open-Ui 'FM' 'FM')
        Assert-FmAudioProgress
      }
      Assert-Health
      Write-SoakLine "RTL_UI_SOAK_CYCLE cycle=$cycle pass=1"
    }
    Write-SoakLine "RTL_UI_SOAK_RESULT pass=1 cycles=$Cycles lines=$script:linesSeen"
  } finally {
    try {
      if ($initial.Band -in @('FM','AM','WX','CB','LORA','BROWSE','ADSB','P25')) {
        [void](Send-And-Wait "RTL_TUNE $($initial.Band) $($initial.Frequency)" '^RTL_TUNE_(?:OK|UNAVAILABLE|INVALID)')
      }
      [void](Send-And-Wait "RTL_UI OPEN $($initial.Screen)" '^RTL_UI_OPEN_(?:OK|INVALID)')
      if ($soundWasEnabled -eq $false) { [void](Send-And-Wait 'RTL_SOUND OFF' '^RTL_SOUND_OK enabled=0$') }
      if ($null -ne $initialVerbosity) {
        [void](Send-And-Wait "RTL_SERIAL VERBOSITY $initialVerbosity" "^RTL_SERIAL_VERBOSITY_OK mode=$initialVerbosity$")
      }
    } catch {
      Write-Warning "Could not restore initial device state: $($_.Exception.Message)"
    }
  }
} catch {
  Write-SoakLine "RTL_UI_SOAK_RESULT pass=0 error=$($_.Exception.Message)"
  Capture-ResetEvidence
  throw
} finally {
  if ($null -ne $script:serial -and $script:serial.IsOpen) { $script:serial.Close() }
}
