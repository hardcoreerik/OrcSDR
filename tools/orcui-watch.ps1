# Read-only OrcUI development-branch watcher for local Windows validation.
# From repository root: .\tools\orcui-watch.ps1 -Once
# Or: .\tools\orcui-watch.ps1 -IntervalSeconds 120
# Ctrl+C stops. Never checks out, resets, builds, flashes, or pushes.
[CmdletBinding()]
param(
    [string]$Branch = 'codex/orcui-adaptive-display-poc',
    [ValidateRange(30,3600)][int]$IntervalSeconds = 120,
    [switch]$Once
)
$ErrorActionPreference = 'Stop'
$repo = (& git rev-parse --show-toplevel 2>$null | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($repo)) {
    throw 'Run from inside an OrcSDR git clone.'
}
$repo = $repo.Trim()
$runFolder = (Get-Location).Path
$logFile = Join-Path $runFolder ('orcui-watch-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$stateFile = Join-Path $runFolder '.orcui-last-seen-sha'
function Write-RunLog([string]$message) {
    $line = '[{0}] {1}' -f (Get-Date -Format 'yyyy-MM-ddTHH:mm:sszzz'), $message
    Write-Host $line
    Add-Content -LiteralPath $logFile -Value $line
}
$lastSeen = ''
if (Test-Path -LiteralPath $stateFile) {
    $lastSeen = (Get-Content -LiteralPath $stateFile -Raw).Trim()
}
Write-RunLog ('OrcUI watch started; branch={0}; interval={1}s; log={2}' -f $Branch,$IntervalSeconds,$logFile)
Write-RunLog 'Read-only: fetch only. Verify build, port and board separately before flashing.'
do {
    try {
        $fetchSpec = 'refs/heads/{0}:refs/remotes/origin/{0}' -f $Branch
        & git -C $repo fetch --quiet origin $fetchSpec 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw ('git fetch failed: ' + $LASTEXITCODE) }
        $remoteRef = 'refs/remotes/origin/{0}' -f $Branch
        $newSha = (& git -C $repo rev-parse --verify $remoteRef 2>&1 | Select-Object -First 1)
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($newSha)) {
            throw 'Could not resolve remote OrcUI branch SHA.'
        }
        $newSha = $newSha.Trim()
        if ($newSha -ne $lastSeen) {
            $summary = (& git -C $repo show -s --format='%h %s' $newSha | Select-Object -First 1)
            Write-RunLog ('NEW_SHA {0} — {1}' -f $newSha,$summary)
            Set-Content -LiteralPath $stateFile -Value $newSha -NoNewline
            $lastSeen = $newSha
        } elseif ($Once) {
            Write-RunLog ('UNCHANGED ' + $newSha)
        }
    } catch {
        Write-RunLog ('FETCH_ERROR ' + $_.Exception.Message)
        if ($Once) { throw }
    }
    if ($Once) { break }
    Start-Sleep -Seconds $IntervalSeconds
} while ($true)
