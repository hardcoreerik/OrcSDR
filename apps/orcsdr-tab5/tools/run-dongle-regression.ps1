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
    soak       -SoakMinutes of the UI soak (about 100 s per cycle); Full level only

  Levels: Quick = preflight, stress (2 rounds), gain.  Standard = + smoke, bands.  Full = + soak.
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
  [string]$CompareTo,
  [string]$Antenna = 'dipole',
  [ValidateSet('preflight', 'smoke', 'stress', 'gain', 'bands', 'hotplug', 'soak')][string[]]$Skip = @()
)

$ErrorActionPreference = 'Stop'
$toolsDir = $PSScriptRoot
$appDir = Split-Path $toolsDir -Parent
$regression = Join-Path $toolsDir 'run-tab5-ui-regression.ps1'
$analyzeIq = Join-Path $toolsDir 'analyze_rtl_iq.py'
$bandSnr = Join-Path $toolsDir 'rtl-gate\band_snr.py'
$capturePy = Join-Path $toolsDir 'rtl-gate\serial_capture.py'
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
$bandsStage = $stages | Where-Object { $_.name -eq 'bands' } | Select-Object -First 1
if ($bandsStage -and $bandsStage.data -and $bandsStage.data.Count) {
  $md.Add(''); $md.Add('| Band (MHz) | Noise floor (dBFS) | Strongest signal over noise (dB) | Offset (kHz) | Strong bins | Clipping % |'); $md.Add('|---|---|---|---|---|---|')
  foreach ($k in $bandsStage.data.Keys) { $v = $bandsStage.data[$k]; $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} |" -f ([double]$k / 1e6), $v.noise_dbfs, $v.peak_over_noise_db, $v.peak_offset_khz, $v.strong_bins, $v.clipping_pct)) }
}
$md | Set-Content (Join-Path $runDir 'report.md') -Encoding utf8
Write-Host ("`nReport: {0}`nResults: {1}" -f (Join-Path $runDir 'report.md'), (Join-Path $runDir 'results.json'))
if ($failed -gt 0) { Write-Host ("{0} stage(s) FAILED" -f $failed) -ForegroundColor Red; exit 1 }
Write-Host 'No stage failed.' -ForegroundColor Green
