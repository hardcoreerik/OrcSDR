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
  [switch]$SelfCheck
)

$ErrorActionPreference = 'Stop'
$script:serial = $null
$script:linesSeen = 0
$script:soakLogPath = $null

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
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $line = $script:serial.ReadLine().Trim()
      if (!$line) { continue }
      $script:linesSeen++
      Write-SoakLine $line
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

function Wait-DeviceReady([int]$Seconds = 60) {
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
      if ($line -match '^RTL_HEALTH_STATUS ') { return }
    } catch [System.TimeoutException] {}
  }
  throw 'Timed out waiting for device readiness.'
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
}

function Get-UiState {
  $line = Send-And-Wait 'RTL_UI STATUS' '^RTL_UI_STATUS '
  if ($line -notmatch 'screen=(\S+) band=(\S+) frequency_hz=(\d+) settings=([01]) fm=([01]) p25=([01]) adsb=([01]) lora=([01]) home_font=([01])') {
    throw "Malformed UI status: $line"
  }
  return [pscustomobject]@{
    Screen = $Matches[1].ToUpperInvariant()
    Band = $Matches[2].ToUpperInvariant()
    Frequency = [uint32]$Matches[3]
    Active = @([int]$Matches[4], [int]$Matches[5], [int]$Matches[6], [int]$Matches[7], [int]$Matches[8])
    HomeFont = [int]$Matches[9]
  }
}

function Test-ExclusiveScreen($State, [string]$Screen) {
  $expected = switch ($Screen) {
    'FM' { @(0,1,0,0,0) }
    'P25' { @(0,0,1,0,0) }
    'ADSB' { @(0,0,0,1,0) }
    'LORA' { @(0,0,0,0,1) }
    'SETTINGS' { @(1,0,0,0,0) }
    'HOME' { @(0,0,0,0,0) }
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
        ($Screen -ne 'HOME' -or $state.HomeFont -eq 1) -and
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

function Assert-FmAudioProgress {
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

function Assert-Health {
  $line = Send-And-Wait 'RTL_HEALTH' '^RTL_HEALTH_STATUS '
  if ($line -notmatch 'free_heap=(\d+) min_free_heap=(\d+) dma_free=(\d+) dma_min=(\d+) dma_largest=(\d+)') {
    throw "Malformed health status: $line"
  }
  if ([uint64]$Matches[1] -eq 0 -or [uint64]$Matches[3] -eq 0 -or [uint64]$Matches[5] -eq 0) {
    throw "Device reported exhausted heap: $line"
  }
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

function Invoke-SelfCheck {
  if (-not (Test-FatalLine 'Guru Meditation Error: Core 1 panic')) { throw 'Fatal parser missed panic.' }
  if (-not (Test-FatalLine 'ESP-ROM:esp32p4-eco2-20240710')) { throw 'Fatal parser missed reset.' }
  if (Test-FatalLine 'RTL_UI_STATUS screen=home band=FM frequency_hz=96144000') {
    throw 'Fatal parser rejected healthy telemetry.'
  }
  $audio = '{"type":"rtl_audio_test","speaker_enabled":1,"speaker_running":1,"audio_chunks":42}' | ConvertFrom-Json
  if ($audio.speaker_running -ne 1 -or $audio.audio_chunks -ne 42) { throw 'Audio parser failed.' }
  if (-not (Test-ExclusiveScreen ([pscustomobject]@{ Active = @(0,0,0,1,0) }) 'ADSB')) {
    throw 'Exclusive dashboard check rejected valid ADS-B state.'
  }
  if (Test-ExclusiveScreen ([pscustomobject]@{ Active = @(0,1,0,1,0) }) 'ADSB') {
    throw 'Exclusive dashboard check accepted stale FM state.'
  }
  Write-SoakLine 'RTL_UI_SOAK_SELF_CHECK pass=1'
}

if ($SelfCheck) { Invoke-SelfCheck; exit 0 }
if ($Run -and $Soak) { throw 'Choose either -Run or -Soak.' }

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

  if ($Profile) {
    $commit = (& git -C (Join-Path $PSScriptRoot '..\..\..') rev-parse --short HEAD 2>$null)
    Write-SoakLine "RTL_UI_SOAK_BEGIN profile=$Profile cycles=$Cycles seed=$Seed port=$Port commit=$commit"
    Write-SoakLine "RTL_UI_SOAK_LOG path=$($script:soakLogPath)"
  }

  if (-not $Soak) {
    if ($Run) { Connect-Authenticated }
    $command = if ($Run) { 'RTL_UI_REGRESSION RUN' } else { 'RTL_UI_REGRESSION CHECK' }
    $line = Send-And-Wait $command '^RTL_UI_REGRESSION_RESULT '
    if ($line -notmatch ' pass=1 ') { throw "UI regression failed: $line" }
    exit 0
  }

  Wait-DeviceReady
  [void](Send-And-Wait 'RTL_STATUS' '^RTL_SDR_STATUS ' 20)
  Connect-Authenticated
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
      [void](Open-Ui 'SETTINGS' $targets[-1].Band)
      Watch-Responsive $DwellSeconds 'SETTINGS' $targets[-1].Band
      [void](Open-Ui 'HOME' $targets[-1].Band)
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
