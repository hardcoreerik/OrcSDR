param(
  [Parameter(Mandatory = $true)]
  [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
  [string]$BackupPath,
  [ValidatePattern('^COM[0-9]+$')]
  [string]$Port = 'COM17',
  [ValidateRange(8, 60)]
  [int]$SurveySeconds = 16,
  [ValidateRange(10, 600)]
  [int]$ControlSoakSeconds = 60
)

$ErrorActionPreference = 'Stop'
$nvsOffset = 0x9000
$nvsLength = 0x6000
$python = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$nvsTool = 'C:\Espressif\frameworks\esp-idf-v5.5.3\components\nvs_flash\nvs_partition_tool\nvs_tool.py'

if (-not (Test-Path -LiteralPath $python -PathType Leaf) -or
    -not (Test-Path -LiteralPath $nvsTool -PathType Leaf)) {
  throw 'ESP-IDF 5.5.3 NVS tools are not installed.'
}

$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$tempNvs = [IO.Path]::Combine(
  $tempRoot, 'orcsdr-p25-nvs-' + [guid]::NewGuid().ToString('N') + '.bin')
$serial = $null
$hmac = $null
$pairingKey = $null

function Wait-ForLine {
  param(
    [Parameter(Mandatory = $true)] [System.IO.Ports.SerialPort]$Serial,
    [Parameter(Mandatory = $true)] [scriptblock]$Match,
    [int]$TimeoutMs = 4000
  )
  $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $line = $Serial.ReadLine().Trim()
      if (& $Match $line) { return $line }
    } catch [System.TimeoutException] {}
  }
  return $null
}

try {
  $backup = [IO.File]::OpenRead((Resolve-Path -LiteralPath $BackupPath).Path)
  try {
    if ($backup.Length -lt $nvsOffset + $nvsLength) {
      throw 'Backup is too small to contain the OrcSDR NVS partition.'
    }
    $backup.Position = $nvsOffset
    $buffer = [byte[]]::new($nvsLength)
    if ($backup.Read($buffer, 0, $buffer.Length) -ne $buffer.Length) {
      throw 'Could not read the complete NVS partition from the backup.'
    }
    [IO.File]::WriteAllBytes($tempNvs, $buffer)
  } finally {
    $backup.Dispose()
  }

  $nvsJson = (& $python $nvsTool -d minimal -f json $tempNvs 2>$null) |
    ConvertFrom-Json
  $pairingEntry = $nvsJson |
    Where-Object { $_.namespace -eq 'orclink' -and $_.key -eq 'pair_key' } |
    Select-Object -First 1
  if ($null -eq $pairingEntry) { throw 'The backup has no OrcSDR pairing key.' }
  $pairingKey = [Convert]::FromBase64String($pairingEntry.data)
  if ($pairingKey.Length -ne 32) { throw 'The OrcSDR pairing key is invalid.' }

  $serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
  $serial.ReadTimeout = 200
  $serial.NewLine = "`n"
  $serial.DtrEnable = $false
  $serial.RtsEnable = $false
  $serial.Open()
  Start-Sleep -Milliseconds 500
  $serial.DiscardInBuffer()

  $nonce = [byte[]]::new(16)
  [Security.Cryptography.RandomNumberGenerator]::Fill($nonce)
  $hmac = [Security.Cryptography.HMACSHA256]::new($pairingKey)
  $hostInput = [Text.Encoding]::ASCII.GetBytes('host') + $nonce
  $hostProof = $hmac.ComputeHash($hostInput)
  $serial.WriteLine('AUTH ' + [Convert]::ToHexString($nonce) + ' ' +
                    [Convert]::ToHexString($hostProof))
  $authLine = Wait-ForLine $serial { param($line) $line -match '^AUTH_' }
  if ($authLine -notmatch '^AUTH_OK ([0-9A-Fa-f]{64})$') {
    throw "Device authentication failed: $authLine"
  }
  $deviceProof = [Convert]::FromHexString($Matches[1])
  $expectedProof = $hmac.ComputeHash([Text.Encoding]::ASCII.GetBytes('device') + $nonce)
  if (-not [Security.Cryptography.CryptographicOperations]::FixedTimeEquals(
      $deviceProof, $expectedProof)) {
    throw 'Device authentication proof did not match.'
  }
  Write-Output 'P25_VALIDATION_AUTH verified=true'

  $serial.WriteLine('RTL_TUNE P25 453812500')
  $tuneLine = Wait-ForLine $serial { param($line) $line -match '^RTL_TUNE_' } 5000
  if ($tuneLine -notmatch '^RTL_TUNE_OK') { throw "P25 tune failed: $tuneLine" }
  Write-Output $tuneLine

  Write-Output 'P25_VALIDATION_AUTO_RECOVERY waiting=true candidates=4'

  $samples = [System.Collections.Generic.List[string]]::new()
  $surveyDone = $null
  $probeFallback = $false
  $lastPing = [DateTime]::UtcNow
  $deadline = [DateTime]::UtcNow.AddSeconds($SurveySeconds)
  while ([DateTime]::UtcNow -lt $deadline -and $null -eq $surveyDone) {
    if (([DateTime]::UtcNow - $lastPing).TotalSeconds -ge 2) {
      $serial.WriteLine('PING')
      $lastPing = [DateTime]::UtcNow
    }
    try {
      $line = $serial.ReadLine().Trim()
      if ($line -match '^RTL_P25_SURVEY_SAMPLE ') {
        $samples.Add($line)
        Write-Output $line
      } elseif ($line -match '^RTL_P25_PROBE no_control fallback=survey$') {
        $probeFallback = $true
        Write-Output $line
      } elseif ($line -match '^RTL_P25_SURVEY_DONE ') {
        $surveyDone = $line
        Write-Output $line
      } elseif ($line -match 'Guru Meditation|rst:') {
        throw "Device reset during P25 survey: $line"
      }
    } catch [System.TimeoutException] {}
  }

  if (-not $probeFallback -or $samples.Count -ne 4 -or $null -eq $surveyDone) {
    throw "P25 automatic recovery incomplete: fallback=$probeFallback samples=$($samples.Count)"
  }

  $decoded = $surveyDone -match 'decoded=1'
  if (-not $decoded) { exit 2 }

  Write-Output "P25_VALIDATION_SOAK started=true seconds=$ControlSoakSeconds"
  $identitySeen = $false
  $grantSeen = $false
  $voiceFollowSeen = $false
  $imbeFrames = 0
  $imbeErrors = 0
  $voiceQueueDrops = 0
  $imbeMaxUs = 0
  $synthMaxUs = 0
  $voiceStackHwm = 0
  $lastStatus = [DateTime]::MinValue
  $soakDeadline = [DateTime]::UtcNow.AddSeconds($ControlSoakSeconds)
  while ([DateTime]::UtcNow -lt $soakDeadline) {
    if (([DateTime]::UtcNow - $lastPing).TotalSeconds -ge 2) {
      $serial.WriteLine('PING')
      $lastPing = [DateTime]::UtcNow
    }
    if (([DateTime]::UtcNow - $lastStatus).TotalSeconds -ge 3) {
      $serial.WriteLine('RTL_P25_STATUS')
      $lastStatus = [DateTime]::UtcNow
    }
    try {
      $line = $serial.ReadLine().Trim()
      if ($line -match '^RTL_P25_STATUS ') {
        Write-Output $line
        $identitySeen = $identitySeen -or $line -match ' identity=1 '
        $grantSeen = $grantSeen -or $line -match ' grants=1 '
        if ($line -match ' imbe_frames=([0-9]+) ') {
          $imbeFrames = [Math]::Max($imbeFrames, [int]$Matches[1])
        }
        if ($line -match ' imbe_errors=([0-9]+) ') {
          $imbeErrors = [Math]::Max($imbeErrors, [int]$Matches[1])
        }
        if ($line -match ' voice_queue_drops=([0-9]+) ') {
          $voiceQueueDrops = [Math]::Max($voiceQueueDrops, [int]$Matches[1])
        }
        if ($line -match ' voice_stack_hwm=([0-9]+) ') {
          $stack = [int]$Matches[1]
          if ($stack -gt 0 -and ($voiceStackHwm -eq 0 -or $stack -lt $voiceStackHwm)) {
            $voiceStackHwm = $stack
          }
        }
        if ($line -match ' imbe_max_us=([0-9]+) ') {
          $imbeMaxUs = [Math]::Max($imbeMaxUs, [int]$Matches[1])
        }
        if ($line -match ' imbe_synth_max_us=([0-9]+) ') {
          $synthMaxUs = [Math]::Max($synthMaxUs, [int]$Matches[1])
        }
      } elseif ($line -match '^RTL_P25_FOLLOW_(VOICE|RETURN) ') {
        Write-Output $line
        $voiceFollowSeen = $voiceFollowSeen -or $line -match '^RTL_P25_FOLLOW_VOICE '
      } elseif ($line -match 'Guru Meditation|rst:') {
        Write-Output $line
        $panicDeadline = [DateTime]::UtcNow.AddSeconds(5)
        while ([DateTime]::UtcNow -lt $panicDeadline) {
          try { Write-Output $serial.ReadLine().Trim() } catch [System.TimeoutException] {}
        }
        throw 'Device reset during P25 soak.'
      }
    } catch [System.TimeoutException] {}
  }

  $result = if ($voiceFollowSeen -and $imbeFrames -gt 0 -and $voiceQueueDrops -gt 0) {
    'VOICE_DECODED_DROPS'
  } elseif ($voiceFollowSeen -and $imbeFrames -gt 0 -and $imbeMaxUs -ge 18000) {
    'VOICE_DECODED_SLOW'
  } elseif ($voiceFollowSeen -and $imbeFrames -gt 0) {
    'VOICE_DECODED'
  } elseif ($grantSeen -or $voiceFollowSeen) {
    'GRANT_SEEN_NO_AUDIO'
  } elseif ($identitySeen) {
    'CONTROL_LOCK_NO_GRANT'
  } else {
    'CONTROL_LOCK_NO_IDENTITY'
  }
  Write-Output (
    "P25_VALIDATION_RESULT result=$result samples=$($samples.Count) " +
    "identity=$([int]$identitySeen) grant=$([int]$grantSeen) " +
    "voice_follow=$([int]$voiceFollowSeen) imbe_frames=$imbeFrames " +
    "imbe_errors=$imbeErrors voice_queue_drops=$voiceQueueDrops " +
    "voice_stack_hwm=$voiceStackHwm imbe_max_us=$imbeMaxUs " +
    "imbe_synth_max_us=$synthMaxUs")
  if ($result -ne 'VOICE_DECODED') { throw "P25 validation failed: $result" }
} finally {
  if ($null -ne $serial -and $serial.IsOpen) { $serial.Close() }
  if ($null -ne $hmac) { $hmac.Dispose() }
  if ($null -ne $pairingKey) {
    [Security.Cryptography.CryptographicOperations]::ZeroMemory($pairingKey)
  }
  $resolvedTemp = [IO.Path]::GetFullPath($tempNvs)
  if ($resolvedTemp.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
      [IO.File]::Exists($resolvedTemp)) {
    [IO.File]::Delete($resolvedTemp)
  }
}
