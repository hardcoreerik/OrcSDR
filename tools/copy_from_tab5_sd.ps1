param(
    [Parameter(Position = 0)]
    [string]$Path,

    [string]$Destination = '.',

    [string]$Port = 'COM17',

    [switch]$LatestRecording,

    [switch]$List
)

$ErrorActionPreference = 'Stop'
$serial = [IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.NewLine = "`n"
$serial.ReadTimeout = 5000
$serial.WriteTimeout = 5000

function Wait-Tab5Line([string[]]$Prefixes, [int]$TimeoutSeconds = 15) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $serial.ReadLine().Trim()
            foreach ($prefix in $Prefixes) {
                if ($line.StartsWith($prefix, [StringComparison]::Ordinal)) { return $line }
            }
        } catch [TimeoutException] {}
    }
    throw 'Timed out waiting for Tab5 response.'
}

function Get-Tab5Files {
    $serial.WriteLine('SD_LIST')
    $files = @()
    while ($true) {
        $line = Wait-Tab5Line @('SD_LIST_ENTRY', 'SD_LIST_DONE', 'SD_LIST_ERROR')
        if ($line.StartsWith('SD_LIST_ERROR')) { throw $line }
        if ($line.StartsWith('SD_LIST_DONE')) { return $files }
        if ($line -notmatch '^SD_LIST_ENTRY bytes=(\d+) modified=(\d+) pathhex=([0-9a-f]+)$') {
            throw "Invalid list response: $line"
        }
        $files += [pscustomobject]@{
            Path = [Text.Encoding]::ASCII.GetString([Convert]::FromHexString($Matches[3]))
            Bytes = [uint64]$Matches[1]
            Modified = [uint64]$Matches[2]
        }
    }
}

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $files = Get-Tab5Files
    if ($List -or (-not $Path -and -not $LatestRecording)) {
        $files | Sort-Object Modified, Path -Descending
        return
    }
    if ($LatestRecording) {
        $Path = ($files | Where-Object Path -Match '^/orcsdr/rec_.*\.wav$' |
            Sort-Object Modified, Path -Descending | Select-Object -First 1).Path
        if (-not $Path) { throw 'No recording WAV exists under /orcsdr/.' }
    }
    if ($Path -notmatch '^/orcsdr/[^.].*$' -or $Path.Contains('..') -or $Path.Contains('\')) {
        throw 'Path must be below /orcsdr/ without .. or backslashes.'
    }
    $destinationItem = Get-Item -LiteralPath $Destination -ErrorAction SilentlyContinue
    if ($destinationItem -and $destinationItem.PSIsContainer) {
        $Destination = Join-Path $destinationItem.FullName ([IO.Path]::GetFileName($Path))
    } else {
        $Destination = [IO.Path]::GetFullPath($Destination)
    }
    if (Test-Path -LiteralPath $Destination) { throw "Destination already exists: $Destination" }

    $pathHex = [Convert]::ToHexString([Text.Encoding]::ASCII.GetBytes($Path)).ToLowerInvariant()
    $serial.WriteLine("SD_GET_BEGIN $pathHex")
    $ready = Wait-Tab5Line @('SD_GET_READY', 'SD_GET_ERROR')
    if ($ready.StartsWith('SD_GET_ERROR')) { throw $ready }
    if ($ready -notmatch 'bytes=(\d+)') { throw "Invalid ready response: $ready" }
    $expected = [uint64]$Matches[1]
    $temporary = "$Destination.part"
    $output = [IO.File]::Create($temporary)
    try {
        $received = 0L
        while ($received -lt $expected) {
            $serial.WriteLine('SD_GET_CHUNK')
            $data = Wait-Tab5Line @('SD_GET_DATA', 'SD_GET_ERROR')
            if ($data.StartsWith('SD_GET_ERROR')) { throw $data }
            if ($data -notmatch '^SD_GET_DATA bytes=(\d+)$') { throw "Invalid data response: $data" }
            $count = [int]$Matches[1]
            $buffer = [byte[]]::new($count)
            $offset = 0
            while ($offset -lt $count) {
                $got = $serial.Read($buffer, $offset, $count - $offset)
                if ($got -le 0) { throw 'Serial transfer ended early.' }
                $offset += $got
            }
            $output.Write($buffer, 0, $buffer.Length)
            $received += $count
            Write-Progress -Activity 'Copying from Tab5 microSD' -Status "$received / $expected bytes" `
                -PercentComplete (($received * 100.0) / $expected)
        }
    } finally {
        $output.Dispose()
    }
    $done = Wait-Tab5Line @('SD_GET_DONE', 'SD_GET_ERROR') 30
    if ($done.StartsWith('SD_GET_ERROR')) { throw $done }
    if ($done -notmatch 'sha256=([0-9a-f]{64})') { throw "Invalid completion response: $done" }
    $deviceSha = $Matches[1]
    $localSha = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($localSha -ne $deviceSha) { throw "SHA-256 mismatch: device=$deviceSha local=$localSha" }
    Move-Item -LiteralPath $temporary -Destination $Destination
    Write-Progress -Activity 'Copying from Tab5 microSD' -Completed
    [pscustomobject]@{ Path = $Destination; Bytes = $expected; SHA256 = $localSha; Source = $Path }
} catch {
    if ($serial.IsOpen) {
        try { $serial.WriteLine('SD_GET_ABORT') } catch {}
    }
    if ($temporary -and (Test-Path -LiteralPath $temporary)) {
        Remove-Item -LiteralPath $temporary
    }
    throw
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
