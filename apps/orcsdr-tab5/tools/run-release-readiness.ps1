param(
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port = 'COM17',
  [string]$PairingKeyPath = (Join-Path $PSScriptRoot '..\..\..\.orclink\ui-doc.key'),
  [string]$LoraCapture,
  [string]$ReportDirectory,
  [switch]$SelfCheck
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$pwsh = (Get-Process -Id $PID).Path
$results = [Collections.Generic.List[object]]::new()

function Add-Result([string]$Name, [string]$Status, [double]$Seconds,
                    [string]$Command, [string]$Log, [string]$Details = '') {
  $results.Add([pscustomobject]@{
    Name = $Name; Status = $Status; Seconds = [math]::Round($Seconds, 1)
    Command = $Command; Log = $Log; Details = $Details
  })
}

function Invoke-ReleaseStep([string]$Name, [string]$File, [string[]]$Arguments,
                            [string]$LogPath) {
  $display = (@($File) + $Arguments | ForEach-Object {
    if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
  }) -join ' '
  $started = Get-Date
  try {
    & $File @Arguments 2>&1 | Tee-Object -FilePath $LogPath
    $code = $LASTEXITCODE
    if ($null -eq $code) { $code = 0 }
    if ($code -ne 0) { throw "exit code $code" }
    Add-Result $Name 'PASS' ((Get-Date) - $started).TotalSeconds $display $LogPath
  } catch {
    Add-Result $Name 'FAIL' ((Get-Date) - $started).TotalSeconds $display $LogPath $_.Exception.Message
  }
}

function Write-ReleaseReport([string]$Path, [string]$Commit, [string]$Branch,
                             [string]$CapturePath, [string]$CaptureHash) {
  $failed = @($results | Where-Object Status -eq 'FAIL').Count
  $status = if ($failed) { 'FAIL' } else { 'PASS' }
  $lines = @(
    '# OrcSDR Release Readiness Report', '',
    "- Result: **$status**",
    "- Generated: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm:ss')) UTC",
    "- Branch: ``$Branch``", "- Commit: ``$Commit``", "- Device: ``$Port``",
    "- LoRa capture: ``$CapturePath``", "- Capture SHA-256: ``$CaptureHash``", '',
    '| Gate | Result | Seconds | Details |', '|---|---:|---:|---|'
  )
  foreach ($result in $results) {
    $details = ($result.Details -replace '\|', '\|' -replace "`r?`n", ' ')
    if (!$details) { $details = "log: $($result.Log)" }
    $lines += "| $($result.Name) | $($result.Status) | $($result.Seconds) | $details |"
  }
  $lines += @('', '## Commands')
  foreach ($result in $results) { $lines += "- **$($result.Name):** ``$($result.Command)``" }
  $lines += @('', "Full logs: ``$(Split-Path -Parent $Path)``")
  Set-Content -LiteralPath $Path -Value $lines -Encoding utf8
  return $status
}

if ($SelfCheck) {
  $temporary = Join-Path ([IO.Path]::GetTempPath()) "orcsdr-release-$([guid]::NewGuid()).md"
  Add-Result 'synthetic pass' 'PASS' 1 'pass' 'pass.log'
  Add-Result 'synthetic fail' 'FAIL' 2 'fail' 'fail.log' 'expected failure'
  $status = Write-ReleaseReport $temporary 'deadbeef' 'self-check' 'capture.orciq' ('0' * 64)
  $text = Get-Content -LiteralPath $temporary -Raw
  Remove-Item -LiteralPath $temporary
  if ($status -ne 'FAIL' -or $text -notmatch 'synthetic pass \| PASS' -or
      $text -notmatch 'synthetic fail \| FAIL' -or $text -notmatch 'deadbeef') {
    throw 'Release report self-check failed.'
  }
  Write-Host 'ORC_RELEASE_SELF_CHECK pass=1'
  exit 0
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (!$ReportDirectory) {
  $ReportDirectory = Join-Path $repo "artifacts\release-readiness\$stamp"
}
$ReportDirectory = [IO.Path]::GetFullPath($ReportDirectory)
[void](New-Item -ItemType Directory -Force -Path $ReportDirectory)
$manifestPath = Join-Path $repo 'tools\lora_lab\corpus_manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$corpusRoot = Join-Path $repo $manifest.corpus_root

if ($LoraCapture) {
  $capture = $manifest.captures | Where-Object {
    [IO.Path]::GetFullPath((Join-Path $corpusRoot $_.path)) -eq
      [IO.Path]::GetFullPath($LoraCapture)
  } | Select-Object -First 1
} else {
  $capture = $manifest.captures | Where-Object {
    $_.classification -eq 'controlled-positive' -and $_.host.result -eq 'pass' -and
    $_.native.result -eq 'pass' -and $_.differential_class -eq 'A' -and
    (Test-Path -LiteralPath (Join-Path $corpusRoot $_.path) -PathType Leaf)
  } | Select-Object -First 1
}
if (!$capture) { throw 'No matching verified positive LoRa corpus capture is available.' }
$capturePath = [IO.Path]::GetFullPath((Join-Path $corpusRoot $capture.path))
$captureHash = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($captureHash -ne $capture.sha256) { throw "LoRa capture hash mismatch: $capturePath" }
if ($null -eq $capture.expected.packet_id) { throw 'Selected capture has no expected packet identity.' }

$commit = (& git -C $repo rev-parse HEAD).Trim()
$branch = (& git -C $repo branch --show-current).Trim()
$python = (Get-Command python -ErrorAction Stop).Source
$uiScript = Join-Path $repo 'apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1'
$buildScript = Join-Path $repo 'apps\orcsdr-tab5\tools\build-tab5-idf.ps1'
$replayScript = Join-Path $repo 'tools\replay_lora_orciq.py'
$iqReport = Join-Path $ReportDirectory 'lora-iq-replay.json'

Invoke-ReleaseStep 'Documentation Truth unit tests' $python @(
  '-m', 'unittest', 'discover', '-s', (Join-Path $repo 'tests'),
  '-p', 'test_documentation_truth.py', '-v'
) (Join-Path $ReportDirectory 'documentation-truth-tests.log')
Invoke-ReleaseStep 'Documentation Truth audit' $python @(
  (Join-Path $repo 'tools\check_documentation_truth.py')
) (Join-Path $ReportDirectory 'documentation-truth-audit.log')
Invoke-ReleaseStep 'Native firmware build' $pwsh @(
  '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript
) (Join-Path $ReportDirectory 'native-build.log')
Invoke-ReleaseStep 'LoRa native IQ replay' $python @(
  $replayScript, $capturePath, '--port', $Port, '--pairing-key', $PairingKeyPath,
  '--compare-host', '--report', $iqReport
) (Join-Path $ReportDirectory 'lora-iq-replay.log')

$replayStep = $results | Where-Object Name -eq 'LoRa native IQ replay' | Select-Object -Last 1
if ($replayStep.Status -eq 'PASS' -and (Test-Path -LiteralPath $iqReport)) {
  $started = Get-Date
  try {
    $iq = Get-Content -LiteralPath $iqReport -Raw | ConvertFrom-Json
    $nativeIds = @($iq.packets | ForEach-Object { [uint64]$_.packet_id })
    $expectedId = [uint64]$capture.expected.packet_id
    if (!$iq.host_decode -or [uint64]$iq.host_packet_id -ne $expectedId -or
        $nativeIds -notcontains $expectedId -or $iq.done -notmatch '\bcrc_ok=[1-9][0-9]*\b') {
      throw "expected_packet_id=$expectedId host_packet_id=$($iq.host_packet_id) native_packet_ids=$($nativeIds -join ',') done=$($iq.done)"
    }
    Add-Result 'LoRa packet identity' 'PASS' ((Get-Date) - $started).TotalSeconds 'validate LoRa replay JSON' $iqReport "expected=host=native=$expectedId crc_ok=1"
  } catch {
    Add-Result 'LoRa packet identity' 'FAIL' ((Get-Date) - $started).TotalSeconds 'validate LoRa replay JSON' $iqReport $_.Exception.Message
  }
} else {
  Add-Result 'LoRa packet identity' 'FAIL' 0 'validate LoRa replay JSON' $iqReport 'Replay did not produce a successful JSON report.'
}

# Running the UI smoke after replay also returns the radio to normal streaming operation.
Invoke-ReleaseStep 'Tab5 serial/UI smoke' $pwsh @(
  '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $uiScript,
  '-Port', $Port, '-PairingKeyPath', $PairingKeyPath, '-Profile', 'Smoke',
  '-LogPath', (Join-Path $ReportDirectory 'tab5-ui-smoke-device.log')
) (Join-Path $ReportDirectory 'tab5-ui-smoke.log')
Invoke-ReleaseStep 'RTL-SDR driver regression' $pwsh @(
  '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $uiScript,
  '-Port', $Port, '-PairingKeyPath', $PairingKeyPath, '-Driver080Rc2'
) (Join-Path $ReportDirectory 'rtl-sdr-driver.log')

$reportPath = Join-Path $ReportDirectory 'release-readiness.md'
$final = Write-ReleaseReport $reportPath $commit $branch $capturePath $captureHash
Write-Host "ORC_RELEASE_READINESS result=$final report=$reportPath"
if ($final -ne 'PASS') { exit 1 }
