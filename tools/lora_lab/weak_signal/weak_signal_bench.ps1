<#
Weak-signal bench: a remote Meshtastic transmitter sends numbered texts while the
Tab5 and a reference Meshtastic node listen. Prints one result row.

Remote side: a computer with a Heltec/T-Beam and meshtastic installed, reachable over SSH,
with remote_tx.py copied to it. Reference side: ref_listen.py on this machine.
The Tab5 is polled with RTL_LORA_RECENT while the batch runs because it keeps only 8 packets.

  .\weak_signal_bench.ps1 -Remote laptop -RemotePort COM3 -RemoteScript C:\lora\remote_tx.py `
      -RefPort COM24 -Tab5Port COM17 -PairingKeyPath .orclink\ui-doc.key -Label V3C_AUTO
#>
param(
  [Parameter(Mandatory)][string]$Remote,
  [Parameter(Mandatory)][string]$RemotePort,
  [Parameter(Mandatory)][string]$RemoteScript,
  [Parameter(Mandatory)][string]$RefPort,
  [Parameter(Mandatory)][string]$Tab5Port,
  [Parameter(Mandatory)][string]$PairingKeyPath,
  [string]$Label = 'run',
  [int]$Count = 10,
  [int]$Gap = 10,
  [string]$Prefix = 'OrcSDR US prof',
  [string]$OutDir = (Join-Path $PSScriptRoot '..\..\..\artifacts\lora_validation\weak_signal')
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$regression = Join-Path $repo 'apps\orcsdr-tab5\tools\run-tab5-ui-regression.ps1'
New-Item -ItemType Directory -Force $OutDir | Out-Null

function Send-Tab5([string[]]$Commands, [string]$Name) {
  $log = Join-Path $OutDir "$Label-$Name.log"
  try { & $regression -Port $Tab5Port -TimeoutSeconds 25 -SendCommand $Commands -PairingKeyPath $PairingKeyPath *> $log } catch {}
  if (-not (Test-Path $log) -or -not (Select-String -Path $log -Pattern 'RTL_UI_SOAK_AUTH verified=1' -Quiet)) {
    throw "Tab5 command failed or was not authenticated ($Name); see $log"
  }
  $log
}
function Heard-Numbers([string]$Log) {
  @(Select-String -Path $Log -Pattern ('text="' + [regex]::Escape($Prefix) + ' (\d+)"') | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
}

$null = Send-Tab5 @('LORA_MESSAGE_CLEAR', 'RTL_LORA_STATS RESET') 'reset'
Start-Sleep 3
$refJson = Join-Path $OutDir "$Label-ref.json"
$listenSeconds = $Count * $Gap + 50
$refJob = Start-Job { python $using:PSScriptRoot\ref_listen.py $using:RefPort $using:listenSeconds $using:refJson }
Start-Sleep 14   # the reference node reboots when its port opens
$t0 = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$txJob = Start-Job {
  ssh.exe -o BatchMode=yes $using:Remote "python $using:RemoteScript $using:RemotePort --count $using:Count --gap $using:Gap --prefix $using:Prefix" 2>&1
}
$seen = New-Object System.Collections.Generic.HashSet[int]
$deadline = (Get-Date).AddSeconds($Count * $Gap + 30)
while ((Get-Date) -lt $deadline) {
  Start-Sleep 28
  foreach ($n in (Heard-Numbers (Send-Tab5 @('RTL_LORA_RECENT') 'poll'))) { [void]$seen.Add($n) }
}
$sent = @(Receive-Job $txJob -Wait -AutoRemoveJob | Where-Object { "$_" -match '"event": "sent"' }).Count
$final = Send-Tab5 @('RTL_LORA_RECENT', 'RTL_LORA_STATS') 'final'
foreach ($n in (Heard-Numbers $final)) { [void]$seen.Add($n) }
Wait-Job $refJob -Timeout 90 | Out-Null; Remove-Job $refJob -Force

$rows = @()
if (Test-Path $refJson) {
  $rows = @((Get-Content $refJson -Raw | ConvertFrom-Json) |
    Where-Object { $_.text -like "$Prefix*" -and $_.t -ge ($t0 - 2) })   # drops stored backlog delivered on connect
}
$refHeard = @($rows.text | Select-Object -Unique).Count
$rssi = @($rows.rssi | Where-Object { $_ } | Sort-Object)
$median = if ($rssi.Count) { $rssi[[int][math]::Floor($rssi.Count / 2)] } else { 'n/a' }
"{0}: sent {1}, reference heard {2} (median RSSI {3} dBm), Tab5 heard {4} {{{5}}}" -f `
  $Label, $sent, $refHeard, $median, $seen.Count, (($seen | Sort-Object) -join ',')
(Select-String -Path $final -Pattern '^RTL_LORA_STATS ' | Select-Object -First 1).Line
