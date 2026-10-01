<#
.SYNOPSIS
  One repeatable regression pass for whichever RTL-SDR dongle is plugged into the Tab5.

.DESCRIPTION
  Runs the stages we otherwise repeat by hand and writes one report (report.md + results.json):

    preflight  identify the dongle, driver version and pinned driver commit; recover a stuck driver
    smoke      the UI smoke profile (run-tab5-ui-regression.ps1 -Profile Smoke)
    stress     band-switch loop (FM, airband, WX, UHF, 915, 1090, HF direct-Q, AM); every switch must
               restart the stream and the driver must never reach FAULT
    gain       FM IQ at three gains: level must rise with gain (skipped when the dongle has no gain)
    bands      IQ per band at a fixed gain: noise floor and strongest signal over noise (-CompareTo diffs
               two reports, which is how a driver change is judged before/after)
    hotplug    optional and interactive (-HotplugCycles N): you unplug and replug the dongle while the
               console is captured; every cycle must be detected, re-probed and restarted, no reboot
    adsb       -AdsbMinutes of ADS-B at 1090 MHz: valid frames per minute, aircraft seen, drops and overruns;
               at -AdsbSoakGain tenths of a dB (default: leave the tuner as the app set it), then -AdsbListSeconds (default 120) of TRACE console capture into an aircraft list
               (adsb-aircraft.csv: callsign, ICAO, altitude range) to check against Flightradar24
    adsbgain   -AdsbGainMinutes per setting on ADS-B: tuner AUTO, then each manual gain in -AdsbGains (tenths of a dB,
               default 28/37.2/49.6 dB). Frames per minute, aircraft and ADC use per gain. HOLD THE ANTENNA STILL.
    adsbtruth  -AdsbTruthMinutes of ADS-B against live truth: polls the adsb.lol community feed for the aircraft within
               -TruthRadiusNm of -ReceiverLat/-ReceiverLon while capturing what the Tab5 decodes (TRACE console), then reports
               the share heard by distance band and altitude band and the aircraft missed. Use -AdsbSoakGain to fix the gain.
               The feed is incomplete (community receivers), so read it as a floor, not an exact score.
    adsbab     -AdsbInterleaveRounds rounds of short windows (-AdsbWindowSeconds) that ALTERNATE the gains in
               -AdsbInterleaveGains (AUTO or tenths of a dB; the order flips every round) while polling the adsb.lol feed.
               Each gain is scored as the share of aircraft in the sky (within -TruthRadiusNm) that the Tab5 decoded in those
               same windows, so changing traffic cannot favour one gain. Test-only: it restores AUTO afterwards.
    pc ref     -PcReferenceGain <dB> adds a second receiver: rtl_adsb on a dongle plugged into the PC (CRC-checked DF17/18, own
               antenna, fixed gain) recording the SAME windows as the Tab5. Reports how much of what the PC heard the Tab5 also
               heard at each gain (a traffic-independent score) and how much of the sky the PC heard.
    soak       -SoakMinutes of the UI soak (about 100 s per cycle); Full level only

  Levels: Quick = preflight, stress (2 rounds), gain.  Standard = + smoke, bands.  Full = + soak.
  -ExpectProfile (blog_v4_r828d, blog_v3_r820t2, blog_v4l_r828s, nooelec_smart_v5_r820t2) stops at the preflight if a
  different dongle is plugged in, so a swapped or unrecognised dongle cannot be measured under the wrong label.
  Everything talks to the device through run-tab5-ui-regression.ps1, so the pairing, auth and splash-gate
  handling stay in one place. Exit code is 0 unless a stage FAILED (WARN and SKIP do not fail the run).

.EXAMPLE
  # Before a driver change, then again after, then compare:
  .\run-dongle-regression.ps1 -Label nooelec-before -Level Standard
  .\run-dongle-regression.ps1 -Label nooelec-after  -Level Standard -CompareTo <path>\nooelec-before\results.json

.EXAMPLE
  # The 15-minute release soak plus five unplug/replug cycles:
  .\run-dongle-regression.ps1 -Label v4 -Level Full -SoakMinutes 15 -HotplugCycles 5

  The device resets during smoke and soak (the script warns first). It never touches the primary checkout.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string]$Label,
  [string]$PairingKeyPath = 'F:\Ai\OrcSDR\.orclink\ui-doc.key',
  [string]$Port = 'COM17',
  [string]$OutputRoot,
  [ValidateSet('Quick', 'Standard', 'Full')][string]$Level = 'Standard',
  [int]$SoakMinutes = 15,
  [int]$StressRounds = 0,
  [int]$HotplugCycles = 0,
  [int]$AdsbMinutes = 0,
  [int]$AdsbListSeconds = 120,
  [int]$AdsbSoakGain = -1,
  [int]$AdsbTruthMinutes = 0,
  [int]$AdsbInterleaveRounds = 0,
  [double]$PcReferenceGain = -1,
  [string]$PcDeviceIndex = '0',
  [int]$AdsbWindowSeconds = 60,
  [string[]]$AdsbInterleaveGains = @('AUTO', '280', '372', '496'),
  [double]$ReceiverLat = 44.05,
  [double]$ReceiverLon = -123.09,
  [int]$TruthRadiusNm = 120,
  [int]$AdsbGainMinutes = 0,
  [int[]]$AdsbGains = @(280, 372, 496),
  [string]$CompareTo,
  [string]$Antenna = 'dipole',
  [ValidateSet('any', 'nooelec_smart_v5_r820t2', 'blog_v3_r820t2', 'blog_v4_r828d', 'blog_v4l_r828s')][string]$ExpectProfile = 'any',
  [ValidateSet('preflight', 'smoke', 'stress', 'gain', 'bands', 'hotplug', 'adsb', 'adsbgain', 'adsbtruth', 'adsbab', 'soak')][string[]]$Skip = @()
)

$ErrorActionPreference = 'Stop'
$toolsDir = $PSScriptRoot
$appDir = Split-Path $toolsDir -Parent
$regression = Join-Path $toolsDir 'run-tab5-ui-regression.ps1'
$analyzeIq = Join-Path $toolsDir 'analyze_rtl_iq.py'
$bandSnr = Join-Path $toolsDir 'rtl-gate\band_snr.py'
$capturePy = Join-Path $toolsDir 'rtl-gate\serial_capture.py'
$pcCapturePy = Join-Path $toolsDir 'rtl-gate\pc_adsb_capture.py'
if (-not $OutputRoot) { $OutputRoot = Join-Path (Resolve-Path (Join-Path $appDir '..\..')).Path 'artifacts\dongle-regression' }
$runDir = Join-Path $OutputRoot ("{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $Label)
New-Item -ItemType Directory -Force $runDir | Out-Null
if ($StressRounds -le 0) { $StressRounds = if ($Level -eq 'Quick') { 2 } else { 4 } }

$stages = New-Object System.Collections.Generic.List[object]
$report = [ordered]@{ label = $Label; level = $Level; started = (Get-Date).ToString('s'); antenna = $Antenna; stages = $stages }

function Add-Stage([string]$Name, [string]$Status, [string]$Detail, $Data = $null) {
  $stages.Add([ordered]@{ name = $Name; status = $Status; detail = $Detail; data = $Data })
  $color = switch ($Status) { 'PASS' { 'Green' } 'FAIL' { 'Red' } 'WARN' { 'Yellow' } default { 'Gray' } }
  Write-Host ("[{0}] {1}: {2}" -f $Status, $Name, $Detail) -ForegroundColor $color
}

# One device command through the existing regression script. $Parameters are its named parameters
# (a hashtable: array splatting would pass them positionally). Returns the output lines.
function Invoke-Reg([hashtable]$Parameters, [string]$LogName) {
  $log = Join-Path $runDir $LogName
  $Parameters['Port'] = $Port
  $Parameters['PairingKeyPath'] = $PairingKeyPath
  # The child throws on a device timeout; log it and carry on so one bad command does not end the run.
  try { & $regression @Parameters *> $log } catch { Add-Content -LiteralPath $log -Value ('EXCEPTION: ' + $_.Exception.Message) }
  return @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue)
}

function Send-Cmd([string]$Command, [string]$LogName) {
  return Invoke-Reg @{ SendCommand = $Command } $LogName
}

function ConvertFrom-KeyValue([string]$Line) {
  $map = @{}
  foreach ($m in [regex]::Matches($Line, '(\w+)=("[^"]*"|\S+)')) { $map[$m.Groups[1].Value] = $m.Groups[2].Value.Trim('"') }
  return $map
}

function Get-DriverStatus([string]$LogName = 'driver-status.log') {
  $line = Send-Cmd 'RTL_DRIVER STATUS' $LogName | Where-Object { $_ -match '^RTL_DRIVER_STATUS ' } | Select-Object -Last 1
  if (-not $line) { return $null }
  return ConvertFrom-KeyValue $line
}

function Restart-Device {
  Write-Host 'Resetting the Tab5 (esptool hard reset)...' -ForegroundColor Yellow
  & python -m esptool --chip esp32p4 -p $Port --after hard_reset read_mac *> (Join-Path $runDir 'reset.log')
  Start-Sleep -Seconds 40
}

# Total increase of a cumulative counter across samples. A drop means the counter was reset (the stream or
# decoder restarted), so the value after the drop counts as new. Returns @(total, resets).
function Get-CounterIncrease($Samples, [string]$Property) {
  $total = [long]0; $resets = 0; $prev = $null
  foreach ($s in $Samples) {
    $v = [long]$s.$Property
    if ($null -ne $prev) { if ($v -ge $prev) { $total += ($v - $prev) } else { $total += $v; $resets++ } }
    $prev = $v
  }
  return @($total, $resets)
}

function Wait-Streaming([int]$TimeoutSeconds = 20) {
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  do {
    $s = Get-DriverStatus 'wait-streaming.log'
    if ($s -and $s['state'] -eq 'STREAMING') { return $s }
    if ($s -and $s['state'] -eq 'FAULT') { return $s }
    Start-Sleep -Seconds 2
  } while ((Get-Date) -lt $deadline)
  return $s
}

# ---------------------------------------------------------------- preflight
$profileName = 'unknown'; $hasGain = $false
if ('preflight' -notin $Skip) {
  $s = Get-DriverStatus
  if ($s -and $s['state'] -eq 'FAULT') { Restart-Device; $s = Get-DriverStatus }
  if (-not $s) { Restart-Device; $s = Get-DriverStatus }
  if (-not $s) {
    Add-Stage 'preflight' 'FAIL' 'no RTL_DRIVER_STATUS from the device (port, pairing key, or firmware?)'
    $report.finished = (Get-Date).ToString('s'); $report | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $runDir 'results.json'); exit 1
  }
  [void](Send-Cmd 'RTL_UI OPEN FM' 'open-fm.log')
  $s = Wait-Streaming 25
  $profileName = $s['profile_name']; $hasGain = ($s['gain_cap'] -eq '1')
  $lockPath = Join-Path $appDir 'dependencies.lock'
  $pin = if (Test-Path $lockPath) { ((Get-Content $lockPath -Raw) -replace '(?s).*?esp_rtl_sdr:.*?version: ([0-9a-f]{7,40}).*', '$1') } else { 'n/a' }
  if ($pin.Length -gt 40) { $pin = 'n/a' }
  $branch = (& git -C $appDir rev-parse --abbrev-ref HEAD 2>$null); $head = (& git -C $appDir rev-parse --short HEAD 2>$null)
  $report.dongle = $profileName; $report.driver_version = $s['version']; $report.driver_pin = $pin
  $report.orcsdr = "$branch@$head"
  $ok = $s['state'] -eq 'STREAMING'
  if ($ExpectProfile -ne 'any' -and $profileName -ne $ExpectProfile) {
    Add-Stage 'preflight' 'FAIL' ("expected dongle {0} but the driver sees {1}: check which dongle is plugged into the Tab5 (replug it, or reset the Tab5)" -f $ExpectProfile, $profileName)
    $report.finished = (Get-Date).ToString('s'); $report | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $runDir 'results.json'); exit 1
  }
  Add-Stage 'preflight' $(if ($ok) { 'PASS' } else { 'FAIL' }) ("{0}, driver {1} (pin {2}), state {3}, gain={4}" -f $profileName, $s['version'], $pin.Substring(0, [Math]::Min(10, $pin.Length)), $s['state'], $hasGain)
  if (-not $ok) { $report.finished = (Get-Date).ToString('s'); $report | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $runDir 'results.json'); exit 1 }
}

# ---------------------------------------------------------------- smoke
if ($Level -ne 'Quick' -and 'smoke' -notin $Skip) {
  Write-Host 'The Tab5 will reset during the smoke profile.' -ForegroundColor Yellow
  $lines = Invoke-Reg @{ Profile = 'Smoke'; Seed = 7 } 'smoke.log'
  $result = $lines | Where-Object { $_ -match 'RTL_UI_SOAK_RESULT' } | Select-Object -Last 1
  $fails = @($lines | Where-Object { $_ -match 'RTL_START ESP_(?!OK)' }).Count
  $pass = $result -match 'pass=1' -and $fails -eq 0
  Add-Stage 'smoke' $(if ($pass) { 'PASS' } else { 'FAIL' }) ("{0}; start failures={1}" -f ($result -replace '^.*RTL_UI_SOAK_RESULT ', ''), $fails)
}

# ---------------------------------------------------------------- stress
$bandSwitches = @(
  @('FM', 96100000), @('BROWSE', 118900000), @('BROWSE', 162550000), @('BROWSE', 453925000),
  @('BROWSE', 915000000), @('BROWSE', 1090000000), @('BROWSE', 7200000), @('AM', 1120000), @('FM', 96100000)
)
if ('stress' -notin $Skip) {
  $n = 0; $bad = @(); $faulted = $false
  for ($round = 1; $round -le $StressRounds -and -not $faulted; $round++) {
    foreach ($b in $bandSwitches) {
      $n++
      $lines = Send-Cmd ("RTL_TUNE {0} {1}" -f $b[0], $b[1]) ("stress-{0:D3}.log" -f $n)
      $err = $lines | Where-Object { $_ -match 'init failed|RTL_START ESP_(?!OK)|EP 0 Error' } | Select-Object -First 1
      if ($err) { $bad += ("#{0} {1}: {2}" -f $n, $b[1], $err.Trim().Substring(0, [Math]::Min(110, $err.Trim().Length))) }
      $s = Wait-Streaming 12
      if (-not $s -or $s['state'] -ne 'STREAMING') {
        $bad += ("#{0} {1}: state {2}" -f $n, $b[1], $(if ($s) { $s['state'] } else { 'no response' }))
        if ($s -and $s['state'] -eq 'FAULT') { $faulted = $true; break }
      }
    }
  }
  Add-Stage 'stress' $(if ($bad.Count -eq 0) { 'PASS' } else { 'FAIL' }) ("{0} band switches over {1} rounds; problems: {2}" -f $n, $StressRounds, $(if ($bad.Count) { ($bad | Select-Object -First 3) -join ' | ' } else { 'none' })) $bad
}

# ---------------------------------------------------------------- IQ helper
function Get-IqCapture([string]$Band, [int]$Hz, $GainTenthDb, [string]$Name) {
  $u8 = Join-Path $runDir "$Name.u8"
  $params = @{ IqDiagnostic = $true; IqBand = $Band; IqFrequency = [uint32]$Hz; IqOutputPath = $u8; IqAntenna = $Antenna; IqAntennaSuitability = 'suitable' }
  if ($null -ne $GainTenthDb) { $params['IqGainTenthDb'] = [int]$GainTenthDb }
  $lines = Invoke-Reg $params "$Name.log"
  if (-not ($lines | Where-Object { $_ -match 'RTL_IQ_DIAGNOSTIC_RESULT pass=1' }) -or -not (Test-Path $u8)) { return $null }
  return $u8
}

# ---------------------------------------------------------------- gain sweep
if ('gain' -notin $Skip) {
  if (-not $hasGain) {
    Add-Stage 'gain' 'SKIP' 'dongle reports no tuner gain'
  } else {
    $rms = @{}
    foreach ($g in 0, 254, 496) {
      $u8 = Get-IqCapture 'FM' 96100000 $g "gain-$g"
      if ($u8) {
        $rep = (& python $analyzeIq $u8 --rate 2400000 2>$null) -join "`n"
        if ($rep -match 'I_RMS=([\d.]+)') { $rms[[string]$g] = [double]$Matches[1] }
      }
    }
    $okSweep = $rms.Count -eq 3 -and $rms['254'] -gt 2 * $rms['0'] -and $rms['496'] -gt $rms['254']
    Add-Stage 'gain' $(if ($okSweep) { 'PASS' } else { 'FAIL' }) ("IQ RMS at 0/25.4/49.6 dB: {0}" -f (($rms.GetEnumerator() | Sort-Object { [int]$_.Name } | ForEach-Object { '{0:N1}' -f $_.Value }) -join ' / ')) $rms
  }
}

# ---------------------------------------------------------------- band table
if ($Level -ne 'Quick' -and 'bands' -notin $Skip) {
  $table = [ordered]@{}
  $bandList = @(@('FM', 96100000), @('BROWSE', 118900000), @('BROWSE', 162550000), @('BROWSE', 453925000), @('BROWSE', 915000000), @('BROWSE', 1090000000))
  foreach ($b in $bandList) {
    $u8 = Get-IqCapture $b[0] $b[1] $(if ($hasGain) { 372 } else { $null }) ("band-{0}" -f $b[1])
    if ($u8) { $table[[string]$b[1]] = (& python $bandSnr $u8 --rate 2400000 2>$null | ConvertFrom-Json) }
  }
  $detail = "{0} of {1} bands captured" -f $table.Count, $bandList.Count
  $status = if ($table.Count -eq $bandList.Count) { 'PASS' } else { 'FAIL' }
  if ($CompareTo -and (Test-Path $CompareTo)) {
    $prev = (Get-Content $CompareTo -Raw | ConvertFrom-Json).stages | Where-Object { $_.name -eq 'bands' } | Select-Object -First 1
    if ($prev -and $prev.data) {
      $diff = foreach ($k in $table.Keys) {
        $p = $prev.data.$k
        if ($p) { "{0} MHz: noise {1:+0.0;-0.0} dB, signal-over-noise {2:+0.0;-0.0} dB" -f ([double]$k / 1e6), ($table[$k].noise_dbfs - $p.noise_dbfs), ($table[$k].peak_over_noise_db - $p.peak_over_noise_db) }
      }
      $detail += '; vs previous: ' + ($diff -join ' | ')
    }
  }
  Add-Stage 'bands' $status $detail $table
}

# ---------------------------------------------------------------- hotplug (interactive)
if ($HotplugCycles -gt 0 -and 'hotplug' -notin $Skip) {
  $prevMode = (Send-Cmd 'RTL_SERIAL VERBOSITY' 'verbosity.log' | Where-Object { $_ -match 'RTL_SERIAL_VERBOSITY mode=(\w+)' } | Select-Object -Last 1)
  $prevMode = if ($prevMode -match 'mode=(\w+)') { $Matches[1] } else { 'NORMAL' }
  [void](Send-Cmd 'RTL_SERIAL VERBOSITY NORMAL' 'verbosity-set.log')
  $seconds = 30 + $HotplugCycles * 35
  $cap = Join-Path $runDir 'hotplug-console.txt'
  $proc = Start-Process -FilePath python -ArgumentList @($capturePy, $Port, $cap, $seconds) -WindowStyle Hidden -PassThru
  Write-Host ("`nUNPLUG and REPLUG the dongle {0} times now. Wait ~10 s unplugged and ~15 s plugged each time. Capture runs {1} s." -f $HotplugCycles, $seconds) -ForegroundColor Cyan
  [void]$proc.WaitForExit($seconds * 1000 + 15000)
  $text = if (Test-Path $cap) { Get-Content $cap } else { @() }
  $count = { param($p) @($text | Where-Object { $_ -match $p }).Count }
  $disc = & $count 'RTL_SDR_DISCONNECTED'; $probe = & $count 'RTL_SDR_PROBE_OK'; $startOk = & $count 'RTL_START ESP_OK'
  $startBad = & $count 'RTL_START ESP_(?!OK)'; $resets = & $count 'RTL_RESET_REASON'; $leaks = & $count 'never retired|leaking'
  $good = $disc -ge $HotplugCycles -and $probe -ge $disc -and $startOk -ge $disc -and $startBad -eq 0 -and $resets -eq 0 -and $leaks -eq 0
  Add-Stage 'hotplug' $(if ($good) { 'PASS' } else { 'FAIL' }) ("asked {0}: disconnects={1} probes={2} restarts={3} start failures={4} reboots={5} leaked URBs={6}" -f $HotplugCycles, $disc, $probe, $startOk, $startBad, $resets, $leaks)
  [void](Send-Cmd "RTL_SERIAL VERBOSITY $prevMode" 'verbosity-restore.log')
  [void](Wait-Streaming 30)
}

# ---------------------------------------------------------------- ADS-B soak
if ($AdsbMinutes -gt 0 -and 'adsb' -notin $Skip) {
  [void](Send-Cmd 'RTL_ADSB_START' 'adsb-start.log')
  Start-Sleep -Seconds 20
  if ($AdsbSoakGain -ge 0) { [void](Send-Cmd "RTL_GAIN MANUAL $AdsbSoakGain" 'adsb-gain-set.log'); Start-Sleep -Seconds 12 }
  $samples = New-Object System.Collections.Generic.List[object]
  $end = (Get-Date).AddMinutes($AdsbMinutes)
  while ((Get-Date) -lt $end) {
    $line = Send-Cmd 'RTL_ADSB STATUS' ('adsb-{0:D3}.log' -f $samples.Count) | Where-Object { $_ -match '^RTL_ADSB_STATUS available=1' } | Select-Object -Last 1
    if ($line) {
      $m = ConvertFrom-KeyValue $line
      $samples.Add([pscustomobject]@{ active = [int]$m['active']; crc_ok = [long]$m['crc_ok']; frames = [long]$m['frames']; aircraft = [int]$m['aircraft']
          overruns = [long]$m['overruns']; drops = [long]$m['drops']; iq_queue_drops = [long]$m['iq_queue_drops']; sps = [long]$m['effective_sps']; signal_dbfs = [double]$m['signal_dbfs'] })
    }
    Start-Sleep -Seconds 15
  }
  if ($samples.Count -lt 3) {
    Add-Stage 'adsb' 'FAIL' ("only {0} usable status samples" -f $samples.Count)
  } else {
    $first = $samples[0]; $last = $samples[$samples.Count - 1]
    $minutes = [Math]::Max(0.5, $AdsbMinutes)
    $perMin = [Math]::Round((Get-CounterIncrease $samples 'crc_ok')[0] / $minutes, 1)
    $maxAircraft = ($samples | Measure-Object aircraft -Maximum).Maximum
    $inactive = @($samples | Where-Object { $_.active -ne 1 }).Count
    $medSps = ($samples | ForEach-Object { $_.sps } | Sort-Object)[[int]($samples.Count / 2)]
    $clean = $last.overruns -eq $first.overruns -and $last.drops -eq $first.drops -and $last.iq_queue_drops -eq $first.iq_queue_drops
    $status = if (-not $clean -or $inactive -gt 0 -or $medSps -lt 1900000) { 'FAIL' } elseif ($perMin -le 0) { 'WARN' } else { 'PASS' }
    $why = if ($status -eq 'WARN') { ' (no valid frames: antenna, location, or no traffic)' } else { '' }
    $adsbData = [ordered]@{ valid_frames_per_min = $perMin; max_aircraft = $maxAircraft; median_sps = $medSps; overruns = ($last.overruns - $first.overruns); drops = ($last.drops - $first.drops); iq_queue_drops = ($last.iq_queue_drops - $first.iq_queue_drops); samples = $samples.Count }
    $cmp = ''
    if ($CompareTo -and (Test-Path $CompareTo)) {
      $prevA = (Get-Content $CompareTo -Raw | ConvertFrom-Json).stages | Where-Object { $_.name -eq 'adsb' } | Select-Object -First 1
      if ($prevA -and $prevA.data) { $cmp = ("; vs previous: {0} frames/min, {1} aircraft" -f $prevA.data.valid_frames_per_min, $prevA.data.max_aircraft) }
    }
    Add-Stage 'adsb' $status ("{0} min: {1} valid frames/min, up to {2} aircraft, median {3} S/s, overruns/drops/queue drops {4}/{5}/{6}{7}{8}" -f $AdsbMinutes, $perMin, $maxAircraft, $medSps, $adsbData.overruns, $adsbData.drops, $adsbData.iq_queue_drops, $why, $cmp) $adsbData
  }
  if ($AdsbListSeconds -gt 0 -and $adsbData) {
    $prevMode = (Send-Cmd 'RTL_SERIAL VERBOSITY' 'adsb-verbosity.log' | Where-Object { $_ -match 'RTL_SERIAL_VERBOSITY mode=(\w+)' } | Select-Object -Last 1)
    $prevMode = if ($prevMode -match 'mode=(\w+)') { $Matches[1] } else { 'NORMAL' }
    [void](Send-Cmd 'RTL_SERIAL VERBOSITY TRACE' 'adsb-verbosity-set.log')
    $capFile = Join-Path $runDir 'adsb-trace-console.txt'
    Write-Host ("Capturing {0} s of ADS-B frames (compare with Flightradar24 for the same minutes)..." -f $AdsbListSeconds) -ForegroundColor Cyan
    $cp = Start-Process -FilePath python -ArgumentList @($capturePy, $Port, $capFile, $AdsbListSeconds) -WindowStyle Hidden -PassThru
    [void]$cp.WaitForExit($AdsbListSeconds * 1000 + 15000)
    [void](Send-Cmd "RTL_SERIAL VERBOSITY $prevMode" 'adsb-verbosity-restore.log')
    $planes = @{}
    foreach ($ln in @(Get-Content -LiteralPath $capFile -ErrorAction SilentlyContinue)) {
      if ($ln -match 'RTL_ADSB_FRAME .*icao=([0-9A-F]{6}) .*callsign=(\S+) altitude_ft=(-?\d+) altitude_valid=(\d)') {
        $icao = $Matches[1]; $cs = $Matches[2]; $alt = [int]$Matches[3]; $altOk = $Matches[4] -eq '1'
        if (-not $planes.ContainsKey($icao)) { $planes[$icao] = [ordered]@{ icao = $icao; callsign = '-'; frames = 0; alt_min = $null; alt_max = $null } }
        $p = $planes[$icao]; $p.frames++
        if ($cs -ne '-') { $p.callsign = $cs }
        if ($altOk) { if ($null -eq $p.alt_min -or $alt -lt $p.alt_min) { $p.alt_min = $alt }; if ($null -eq $p.alt_max -or $alt -gt $p.alt_max) { $p.alt_max = $alt } }
      }
    }
    $rows = @($planes.Values | Sort-Object { -$_.frames })
    $rows | ForEach-Object { [pscustomobject]$_ } | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $runDir 'adsb-aircraft.csv')
    $adsbData['aircraft_list'] = @($rows | ForEach-Object { "{0} {1} {2}-{3} ft ({4} frames)" -f $_.callsign, $_.icao, $_.alt_min, $_.alt_max, $_.frames })
    Write-Host ("ADS-B aircraft heard in {0} s: {1}" -f $AdsbListSeconds, $rows.Count) -ForegroundColor Cyan
    $rows | Select-Object -First 15 | ForEach-Object { Write-Host ("  {0,-9} {1}  {2,6}-{3,6} ft  {4} frames" -f $_.callsign, $_.icao, $_.alt_min, $_.alt_max, $_.frames) }
    $stages[$stages.Count - 1]['detail'] += ("; {0} distinct aircraft in the {1} s list (adsb-aircraft.csv)" -f $rows.Count, $AdsbListSeconds)
  }
  if ($AdsbSoakGain -ge 0) { [void](Send-Cmd 'RTL_GAIN AUTO' 'adsb-gain-restore.log') }
  [void](Send-Cmd 'RTL_UI OPEN FM' 'adsb-restore-fm.log')
}

# ---------------------------------------------------------------- ADS-B gain sweep
if ($AdsbGainMinutes -gt 0 -and 'adsbgain' -notin $Skip) {
  [void](Send-Cmd 'RTL_ADSB_START' 'adsbgain-start.log')
  Start-Sleep -Seconds 20
  $gainRows = [ordered]@{}
  foreach ($setting in @('AUTO') + @($AdsbGains | ForEach-Object { [string]$_ })) {
    $cmd = if ($setting -eq 'AUTO') { 'RTL_GAIN AUTO' } else { "RTL_GAIN MANUAL $setting" }
    [void](Send-Cmd $cmd ("adsbgain-{0}-set.log" -f $setting))
    Start-Sleep -Seconds 12
    $samples = New-Object System.Collections.Generic.List[object]
    $end = (Get-Date).AddMinutes($AdsbGainMinutes)
    while ((Get-Date) -lt $end) {
      $line = Send-Cmd 'RTL_ADSB STATUS' ("adsbgain-{0}-{1:D3}.log" -f $setting, $samples.Count) | Where-Object { $_ -match '^RTL_ADSB_STATUS available=1' } | Select-Object -Last 1
      if ($line) {
        $m = ConvertFrom-KeyValue $line
        $samples.Add([pscustomobject]@{ crc_ok = [long]$m['crc_ok']; preambles = [long]$m['preambles']; aircraft = [int]$m['aircraft']; smin = [int]$m['sample_min']; smax = [int]$m['sample_max']
            drops = [long]$m['drops'] + [long]$m['overruns']; signal = [double]$m['signal_dbfs']; uptime = [long]$m['uptime_ms'] })
      }
      Start-Sleep -Seconds 10
    }
    if ($samples.Count -ge 3) {
      $sigs = @($samples | ForEach-Object { $_.signal } | Sort-Object)
      $crc = Get-CounterIncrease $samples 'crc_ok'; $pre = Get-CounterIncrease $samples 'preambles'
      $restarts = (Get-CounterIncrease $samples 'uptime')[1]
      $gainRows[$setting] = [ordered]@{
        valid_frames_per_min = [Math]::Round($crc[0] / $AdsbGainMinutes, 1)
        preambles_per_min = [Math]::Round($pre[0] / $AdsbGainMinutes, 1)
        stream_restarts = $restarts
        max_aircraft = ($samples | Measure-Object aircraft -Maximum).Maximum
        median_signal_dbfs = $sigs[[int]($sigs.Count / 2)]
        adc_swing = (($samples | Measure-Object smax -Maximum).Maximum - ($samples | Measure-Object smin -Minimum).Minimum)
        clipping = ((($samples | Measure-Object smax -Maximum).Maximum -ge 250) -or (($samples | Measure-Object smin -Minimum).Minimum -le 5))
        drops = (Get-CounterIncrease $samples 'drops')[0]
      }
    }
  }
  [void](Send-Cmd 'RTL_GAIN AUTO' 'adsbgain-restore.log')
  [void](Send-Cmd 'RTL_UI OPEN FM' 'adsbgain-restore-fm.log')
  $best = $gainRows.GetEnumerator() | Sort-Object { $_.Value.valid_frames_per_min } -Descending | Select-Object -First 1
  $auto = $gainRows['AUTO']
  $detail = if ($gainRows.Count -gt 1 -and $best) { "best gain {0} at {1} valid frames/min (AUTO: {2}); {3} min per setting" -f $best.Key, $best.Value.valid_frames_per_min, $(if ($auto) { $auto.valid_frames_per_min } else { 'n/a' }), $AdsbGainMinutes } else { 'too few samples' }
  Add-Stage 'adsbgain' $(if ($gainRows.Count -ge 2) { 'PASS' } else { 'FAIL' }) $detail $gainRows
}

# Initial compass bearing (degrees, 0 = north) from the receiver to an aircraft.
function Get-BearingDeg([double]$Lat2, [double]$Lon2) {
  $rad = [Math]::PI / 180.0
  $p1 = $ReceiverLat * $rad; $p2 = $Lat2 * $rad; $dl = ($Lon2 - $ReceiverLon) * $rad
  $y = [Math]::Sin($dl) * [Math]::Cos($p2); $x = [Math]::Cos($p1) * [Math]::Sin($p2) - [Math]::Sin($p1) * [Math]::Cos($p2) * [Math]::Cos($dl)
  return [int](([Math]::Atan2($y, $x) / $rad + 360) % 360)
}

# ---------------------------------------------------------------- ADS-B truth check
if ($AdsbTruthMinutes -gt 0 -and 'adsbtruth' -notin $Skip) {
  [void](Send-Cmd 'RTL_ADSB_START' 'truth-start.log')
  Start-Sleep -Seconds 20
  if ($AdsbSoakGain -ge 0) { [void](Send-Cmd "RTL_GAIN MANUAL $AdsbSoakGain" 'truth-gain-set.log'); Start-Sleep -Seconds 12 }
  $prevMode = (Send-Cmd 'RTL_SERIAL VERBOSITY' 'truth-verbosity.log' | Where-Object { $_ -match 'RTL_SERIAL_VERBOSITY mode=(\w+)' } | Select-Object -Last 1)
  $prevMode = if ($prevMode -match 'mode=(\w+)') { $Matches[1] } else { 'NORMAL' }
  [void](Send-Cmd 'RTL_SERIAL VERBOSITY TRACE' 'truth-verbosity-set.log')
  $capFile = Join-Path $runDir 'truth-trace-console.txt'
  $seconds = $AdsbTruthMinutes * 60
  $cp = Start-Process -FilePath python -ArgumentList @($capturePy, $Port, $capFile, $seconds) -WindowStyle Hidden -PassThru
  $truth = @{}
  $end = (Get-Date).AddSeconds($seconds)
  Write-Host ("Truth check: {0} min of ADS-B decode against the adsb.lol feed within {1} nm of {2},{3}..." -f $AdsbTruthMinutes, $TruthRadiusNm, $ReceiverLat, $ReceiverLon) -ForegroundColor Cyan
  while ((Get-Date) -lt $end) {
    try {
      $feed = Invoke-RestMethod -Uri ("https://api.adsb.lol/v2/point/{0}/{1}/{2}" -f $ReceiverLat, $ReceiverLon, $TruthRadiusNm) -TimeoutSec 20
      foreach ($a in @($feed.ac)) {
        if (-not $a.hex -or $a.hex.StartsWith('~')) { continue }
        $hex = $a.hex.ToUpper(); $alt = if ("$($a.alt_baro)" -match '^-?\d+$') { [int]"$($a.alt_baro)" } else { 0 }
        if (-not $truth.ContainsKey($hex)) { $truth[$hex] = [ordered]@{ icao = $hex; callsign = (([string]$a.flight).Trim()); min_nm = [double]$a.dst; max_alt = $alt; alt_min = $alt; polls = 0 } }
        $r = $truth[$hex]; $r.polls++
        if ([double]$a.dst -lt $r.min_nm) { $r.min_nm = [double]$a.dst }
        if ($alt -gt $r.max_alt) { $r.max_alt = $alt }; if ($alt -lt $r.alt_min) { $r.alt_min = $alt }
      }
    } catch { }
    Start-Sleep -Seconds 20
  }
  [void]$cp.WaitForExit(20000)
  [void](Send-Cmd "RTL_SERIAL VERBOSITY $prevMode" 'truth-verbosity-restore.log')
  $heard = @{}
  foreach ($ln in @(Get-Content -LiteralPath $capFile -ErrorAction SilentlyContinue)) { if ($ln -match 'RTL_ADSB_FRAME .*icao=([0-9A-F]{6}) ') { $heard[$Matches[1]] = 1 + [int]$heard[$Matches[1]] } }
  $rows = foreach ($k in $truth.Keys) { $r = $truth[$k]; [pscustomobject]@{ icao = $k; callsign = $r.callsign; min_nm = [Math]::Round($r.min_nm, 1); max_alt_ft = $r.max_alt; heard = $heard.ContainsKey($k); frames = [int]$heard[$k] } }
  $rows | Sort-Object min_nm | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $runDir 'adsb-truth.csv')
  $bandOf = { param($nm) if ($nm -lt 25) { '0-25 nm' } elseif ($nm -lt 50) { '25-50 nm' } elseif ($nm -lt 100) { '50-100 nm' } else { '100+ nm' } }
  $altOf = { param($ft) if ($ft -lt 5000) { 'below 5,000 ft' } elseif ($ft -lt 20000) { '5,000-20,000 ft' } else { 'above 20,000 ft' } }
  $byDist = [ordered]@{}; $byAlt = [ordered]@{}
  foreach ($label in '0-25 nm', '25-50 nm', '50-100 nm', '100+ nm') { $g = @($rows | Where-Object { (& $bandOf $_.min_nm) -eq $label }); if ($g.Count) { $byDist[$label] = [ordered]@{ in_sky = $g.Count; heard = @($g | Where-Object heard).Count } } }
  foreach ($label in 'below 5,000 ft', '5,000-20,000 ft', 'above 20,000 ft') { $g = @($rows | Where-Object { (& $altOf $_.max_alt_ft) -eq $label }); if ($g.Count) { $byAlt[$label] = [ordered]@{ in_sky = $g.Count; heard = @($g | Where-Object heard).Count } } }
  $extra = @($heard.Keys | Where-Object { -not $truth.ContainsKey($_) }).Count
  $totalHeard = @($rows | Where-Object heard).Count
  $near = @($rows | Where-Object { $_.min_nm -lt 50 -and $_.max_alt_ft -ge 5000 })
  $nearHeard = @($near | Where-Object heard).Count
  $data = [ordered]@{ in_sky = $rows.Count; heard = $totalHeard; heard_not_in_feed = $extra; by_distance = $byDist; by_altitude = $byAlt; within_50nm_above_5000ft = "$nearHeard of $($near.Count)" }
  $status = if ($rows.Count -eq 0) { 'FAIL' } elseif ($totalHeard -eq 0) { 'FAIL' } else { 'PASS' }
  $d1 = ($byDist.GetEnumerator() | ForEach-Object { "{0}: {1}/{2}" -f $_.Key, $_.Value.heard, $_.Value.in_sky }) -join ', '
  Add-Stage 'adsbtruth' $status ("{0} min: heard {1} of {2} aircraft in the feed ({3}); within 50 nm and above 5,000 ft: {4} of {5}; decoded but not in the feed: {6}" -f $AdsbTruthMinutes, $totalHeard, $rows.Count, $d1, $nearHeard, $near.Count, $extra) $data
  $missed = @($near | Where-Object { -not $_.heard } | Sort-Object min_nm | Select-Object -First 8)
  if ($missed.Count) { Write-Host 'Missed within 50 nm above 5,000 ft:' -ForegroundColor Yellow; $missed | ForEach-Object { Write-Host ("  {0} {1,-8} {2,5} nm  up to {3} ft" -f $_.icao, $_.callsign, $_.min_nm, $_.max_alt_ft) } }
  if ($AdsbSoakGain -ge 0) { [void](Send-Cmd 'RTL_GAIN AUTO' 'truth-gain-restore.log') }
  [void](Send-Cmd 'RTL_UI OPEN FM' 'truth-restore-fm.log')
}

# ---------------------------------------------------------------- ADS-B interleaved gain A/B against live truth
if ($AdsbInterleaveRounds -gt 0 -and 'adsbab' -notin $Skip) {
  [void](Send-Cmd 'RTL_ADSB_START' 'ab-start.log')
  Start-Sleep -Seconds 20
  $prevMode = (Send-Cmd 'RTL_SERIAL VERBOSITY' 'ab-verbosity.log' | Where-Object { $_ -match 'RTL_SERIAL_VERBOSITY mode=(\w+)' } | Select-Object -Last 1)
  $prevMode = if ($prevMode -match 'mode=(\w+)') { $Matches[1] } else { 'NORMAL' }
  [void](Send-Cmd 'RTL_SERIAL VERBOSITY TRACE' 'ab-verbosity-set.log')
  function Get-TruthNow {
    try {
      $feed = Invoke-RestMethod -Uri ("https://api.adsb.lol/v2/point/{0}/{1}/{2}" -f $ReceiverLat, $ReceiverLon, $TruthRadiusNm) -TimeoutSec 20
      $r = @{}
      foreach ($a in @($feed.ac)) { if ($a.hex -and -not $a.hex.StartsWith('~')) { $alt = if ("$($a.alt_baro)" -match '^-?\d+$') { [int]"$($a.alt_baro)" } else { 0 }; $r[$a.hex.ToUpper()] = [pscustomobject]@{ nm = [double]$a.dst; alt = $alt; brg = $(if ($null -ne $a.lat -and $null -ne $a.lon) { Get-BearingDeg ([double]$a.lat) ([double]$a.lon) } else { -1 }) } } }
      return $r
    } catch { return @{} }
  }
  $sectors = 'N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'
  $sec = [ordered]@{}; foreach ($n in $sectors) { $sec[$n] = [ordered]@{ sky = 0; tab5 = 0; pc = 0 } }
  $acc = @{}
  foreach ($g in $AdsbInterleaveGains) { $acc[$g] = [ordered]@{ windows = 0; sky = 0; heard = 0; sky_near = 0; heard_near = 0; sky_far = 0; heard_far = 0; frames = 0; sky_los = 0; heard_los = 0; pc_total = 0; both = 0; pc_in_sky = 0 } }
  for ($round = 1; $round -le $AdsbInterleaveRounds; $round++) {
    $order = if ($round % 2 -eq 1) { $AdsbInterleaveGains } else { @($AdsbInterleaveGains)[($AdsbInterleaveGains.Count - 1)..0] }
    foreach ($g in $order) {
      $cmd = if ($g -eq 'AUTO') { 'RTL_GAIN AUTO' } else { "RTL_GAIN MANUAL $g" }
      [void](Send-Cmd $cmd ("ab-r{0}-{1}-set.log" -f $round, $g))
      Start-Sleep -Seconds 6
      $capFile = Join-Path $runDir ("ab-r{0}-{1}.txt" -f $round, $g)
      $cp = Start-Process -FilePath python -ArgumentList @($capturePy, $Port, $capFile, $AdsbWindowSeconds) -WindowStyle Hidden -PassThru
      $pcCsv = Join-Path $runDir ("ab-r{0}-{1}-pc.csv" -f $round, $g)
      $pcProc = $null
      if ($PcReferenceGain -ge 0) { $pcProc = Start-Process -FilePath python -ArgumentList @($pcCapturePy, $pcCsv, $AdsbWindowSeconds, $PcReferenceGain, $PcDeviceIndex) -WindowStyle Hidden -PassThru }
      $sky = @{}
      foreach ($frac in 0, 0.5, 1) {
        if ($frac -gt 0) { Start-Sleep -Seconds ([int]($AdsbWindowSeconds * 0.5) - 4) }
        $snap = Get-TruthNow
        foreach ($k in $snap.Keys) { if (-not $sky.ContainsKey($k) -or $snap[$k].nm -lt $sky[$k].nm) { $sky[$k] = $snap[$k] } }
      }
      [void]$cp.WaitForExit(($AdsbWindowSeconds + 15) * 1000)
      if ($pcProc) { [void]$pcProc.WaitForExit(30000) }
      $heard = @{}
      foreach ($ln in @(Get-Content -LiteralPath $capFile -ErrorAction SilentlyContinue)) { if ($ln -match 'RTL_ADSB_FRAME .*icao=([0-9A-F]{6}) ') { $heard[$Matches[1]] = 1 + [int]$heard[$Matches[1]] } }
      $pcHeard = @{}
      if ($PcReferenceGain -ge 0 -and (Test-Path $pcCsv)) { foreach ($r in @(Import-Csv -LiteralPath $pcCsv)) { $pcHeard[$r.icao] = 1 + [int]$pcHeard[$r.icao] } }
      $sky.GetEnumerator() | ForEach-Object { [pscustomobject]@{ icao = $_.Key; nm = [Math]::Round($_.Value.nm, 1); alt_ft = $_.Value.alt; bearing_deg = $_.Value.brg; heard_tab5 = $heard.ContainsKey($_.Key); heard_pc = $pcHeard.ContainsKey($_.Key) } } | Sort-Object nm | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $runDir ("ab-r{0}-{1}-sky.csv" -f $round, $g))
      foreach ($k in $sky.Keys) {
        if ($sky[$k].brg -ge 0) { $name = $sectors[[int]([Math]::Floor((($sky[$k].brg + 22.5) % 360) / 45))]; $sec[$name].sky++; if ($heard.ContainsKey($k)) { $sec[$name].tab5++ }; if ($pcHeard.ContainsKey($k)) { $sec[$name].pc++ } }
      }
      $a = $acc[$g]; $a.windows++; $a.frames += ($heard.Values | Measure-Object -Sum).Sum
      $a.pc_total += $pcHeard.Count; foreach ($k in $pcHeard.Keys) { if ($heard.ContainsKey($k)) { $a.both++ } }; foreach ($k in $sky.Keys) { if ($pcHeard.ContainsKey($k)) { $a.pc_in_sky++ } }
      foreach ($k in $sky.Keys) {
        $near = $sky[$k].nm -lt 50; $hit = $heard.ContainsKey($k)
        $a.sky++; if ($hit) { $a.heard++ }
        if ($sky[$k].nm -lt 100 -and $sky[$k].alt -ge 10000) { $a.sky_los++; if ($hit) { $a.heard_los++ } }
        if ($near) { $a.sky_near++; if ($hit) { $a.heard_near++ } } else { $a.sky_far++; if ($hit) { $a.heard_far++ } }
      }
    }
  }
  [void](Send-Cmd "RTL_SERIAL VERBOSITY $prevMode" 'ab-verbosity-restore.log')
  [void](Send-Cmd 'RTL_GAIN AUTO' 'ab-gain-restore.log')
  [void](Send-Cmd 'RTL_UI OPEN FM' 'ab-restore-fm.log')
  $abRows = [ordered]@{}
  foreach ($g in $AdsbInterleaveGains) {
    $a = $acc[$g]
    $abRows[$g] = [ordered]@{ windows = $a.windows; aircraft_windows_in_sky = $a.sky; heard = $a.heard
      share_heard_pct = $(if ($a.sky) { [Math]::Round(100.0 * $a.heard / $a.sky, 1) } else { 0 })
      near_under_50nm = "$($a.heard_near)/$($a.sky_near)"; far_over_50nm = "$($a.heard_far)/$($a.sky_far)"
      los_100nm_above_10kft = "$($a.heard_los)/$($a.sky_los)"; los_share_pct = $(if ($a.sky_los) { [Math]::Round(100.0 * $a.heard_los / $a.sky_los, 1) } else { 0 }); logged_frames = $a.frames
      pc_aircraft_windows = $a.pc_total; tab5_also_heard = $a.both; tab5_share_of_pc_pct = $(if ($a.pc_total) { [Math]::Round(100.0 * $a.both / $a.pc_total, 1) } else { $null }); pc_share_of_sky_pct = $(if ($a.sky) { [Math]::Round(100.0 * $a.pc_in_sky / $a.sky, 1) } else { 0 }) }
  }
  $best = $abRows.GetEnumerator() | Sort-Object { $_.Value.share_heard_pct } -Descending | Select-Object -First 1
  $abRows['_by_sector'] = $sec
  Add-Stage 'adsbab' $(if ($best -and $best.Value.aircraft_windows_in_sky -gt 0) { 'PASS' } else { 'FAIL' }) ("{0} rounds x {1} gains x {2} s; highest share heard: {3} at {4}% (all gains saw the same traffic)" -f $AdsbInterleaveRounds, $AdsbInterleaveGains.Count, $AdsbWindowSeconds, $best.Key, $best.Value.share_heard_pct) $abRows
}

# ---------------------------------------------------------------- soak
if ($Level -eq 'Full' -and 'soak' -notin $Skip) {
  $cycles = [Math]::Max(1, [int][Math]::Ceiling($SoakMinutes * 60 / 100.0))
  Write-Host ("Soak: {0} cycles (about {1} min). The Tab5 resets between cycles." -f $cycles, $SoakMinutes) -ForegroundColor Yellow
  $t0 = Get-Date
  $lines = Invoke-Reg @{ Soak = $true; Cycles = $cycles; DwellSeconds = 12 } 'soak.log'
  $minutes = [Math]::Round(((Get-Date) - $t0).TotalMinutes, 1)
  $result = $lines | Where-Object { $_ -match 'RTL_UI_SOAK_RESULT' } | Select-Object -Last 1
  $passed = @($lines | Where-Object { $_ -match 'RTL_UI_SOAK_CYCLE .*pass=1' }).Count
  $fails = @($lines | Where-Object { $_ -match 'RTL_START ESP_(?!OK)' }).Count
  $wifi = @($lines | Where-Object { $_ -match 'RTL_WIFI_LINK_LOST' }).Count
  $status = if ($result -match 'pass=1' -and $fails -eq 0) { 'PASS' } elseif ($fails -eq 0 -and $wifi -gt 0) { 'WARN' } else { 'FAIL' }
  $note = if ($status -eq 'WARN') { ' (inconclusive: the Wi-Fi co-processor link dropped; the radio side had no failures)' } else { '' }
  Add-Stage 'soak' $status ("{0} min, {1}/{2} cycles passed, start failures={3}, Wi-Fi link losses={4}{5}" -f $minutes, $passed, $cycles, $fails, $wifi, $note)
}

# ---------------------------------------------------------------- report
$report.finished = (Get-Date).ToString('s')
$report | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $runDir 'results.json')
$failed = @($stages | Where-Object { $_.status -eq 'FAIL' }).Count
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Dongle regression: $Label"); $md.Add('')
$md.Add(("- Dongle: **{0}**, driver {1}, pin {2}" -f $report.dongle, $report.driver_version, $report.driver_pin))
$md.Add(("- OrcSDR: {0}, level {1}, antenna {2}, {3}" -f $report.orcsdr, $Level, $Antenna, $report.started)); $md.Add('')
$md.Add('| Stage | Result | Detail |'); $md.Add('|---|---|---|')
foreach ($st in $stages) { $md.Add(("| {0} | {1} | {2} |" -f $st.name, $st.status, ($st.detail -replace '\|', '/'))) }
$abStage = $stages | Where-Object { $_.name -eq 'adsbab' } | Select-Object -First 1
if ($abStage -and $abStage.data -and $abStage.data.Count) {
  $md.Add(''); $md.Add('| ADS-B gain (interleaved) | Windows | Aircraft-windows in sky | Heard | Share heard % | Under 50 nm | Over 50 nm | Line-of-sight candidates (above 10,000 ft, under 100 nm) | PC reference heard | Tab5 caught of PC (%) |'); $md.Add('|---|---|---|---|---|---|---|---|---|---|')
  foreach ($k in $abStage.data.Keys) { if ($k -eq '_by_sector') { continue }; $v = $abStage.data[$k]; $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} ({8}%) | {9} | {10} |" -f $k, $v.windows, $v.aircraft_windows_in_sky, $v.heard, $v.share_heard_pct, $v.near_under_50nm, $v.far_over_50nm, $v.los_100nm_above_10kft, $v.los_share_pct, $v.pc_aircraft_windows, $v.tab5_share_of_pc_pct)) }
}
if ($abStage -and $abStage.data -and $abStage.data['_by_sector']) {
  $md.Add(''); $md.Add('By compass direction from the receiver (all gains pooled): aircraft in the sky, and how many each receiver heard'); $md.Add('')
  $md.Add('| Sector | Aircraft-windows in sky | Tab5 heard | Tab5 % | PC heard | PC % |'); $md.Add('|---|---|---|---|---|---|')
  foreach ($k in $abStage.data['_by_sector'].Keys) { $v = $abStage.data['_by_sector'][$k]; $pt = if ($v.sky) { [Math]::Round(100.0 * $v.tab5 / $v.sky, 1) } else { 0 }; $pp = if ($v.sky) { [Math]::Round(100.0 * $v.pc / $v.sky, 1) } else { 0 }; $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} |" -f $k, $v.sky, $v.tab5, $pt, $v.pc, $pp)) }
}
$gainStage = $stages | Where-Object { $_.name -eq 'adsbgain' } | Select-Object -First 1
if ($gainStage -and $gainStage.data -and $gainStage.data.Count) {
  $md.Add(''); $md.Add('| ADS-B gain | Valid frames/min | Preambles/min | Max aircraft | Median signal (dBFS) | ADC swing | Clipping | Stream restarts |'); $md.Add('|---|---|---|---|---|---|---|---|')
  foreach ($k in $gainStage.data.Keys) { $v = $gainStage.data[$k]; $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} |" -f $k, $v.valid_frames_per_min, $v.preambles_per_min, $v.max_aircraft, $v.median_signal_dbfs, $v.adc_swing, $v.clipping, $v.stream_restarts)) }
}
$bandsStage = $stages | Where-Object { $_.name -eq 'bands' } | Select-Object -First 1
if ($bandsStage -and $bandsStage.data -and $bandsStage.data.Count) {
  $md.Add(''); $md.Add('| Band (MHz) | Noise floor (dBFS) | Strongest signal over noise (dB) | Offset (kHz) | Strong bins | Clipping % |'); $md.Add('|---|---|---|---|---|---|')
  foreach ($k in $bandsStage.data.Keys) { $v = $bandsStage.data[$k]; $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} |" -f ([double]$k / 1e6), $v.noise_dbfs, $v.peak_over_noise_db, $v.peak_offset_khz, $v.strong_bins, $v.clipping_pct)) }
}
$md | Set-Content (Join-Path $runDir 'report.md') -Encoding utf8
Write-Host ("`nReport: {0}`nResults: {1}" -f (Join-Path $runDir 'report.md'), (Join-Path $runDir 'results.json'))
if ($failed -gt 0) { Write-Host ("{0} stage(s) FAILED" -f $failed) -ForegroundColor Red; exit 1 }
Write-Host 'No stage failed.' -ForegroundColor Green
