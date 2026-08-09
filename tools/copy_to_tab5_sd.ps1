param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Path,

    [Parameter(Position = 1)]
    [string]$Destination,

    [string]$Port = 'COM17'
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $Path).Path
if (-not $Destination) {
    $Destination = '/orcsdr/' + [IO.Path]::GetFileName($source)
}
if ($Destination -notmatch '^/orcsdr/[\x20-\x7e]+$' -or $Destination.Contains('..') -or
    $Destination.Contains('\')) {
    throw 'Destination must be an ASCII path below /orcsdr/ without .. or backslashes.'
}

$file = Get-Item -LiteralPath $source
if ($file.Length -le 0 -or $file.Length -gt 64MB) {
    throw 'File must be between 1 byte and 64 MiB.'
}
$sha = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
$pathHex = [Convert]::ToHexString([Text.Encoding]::ASCII.GetBytes($Destination)).ToLowerInvariant()

$serial = [IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.NewLine = "`n"
$serial.ReadTimeout = 500
$serial.WriteTimeout = 5000

function Wait-Tab5Line([string[]]$Prefixes, [int]$TimeoutSeconds = 10) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $line = $serial.ReadLine().Trim()
            foreach ($prefix in $Prefixes) {
                if ($line.StartsWith($prefix, [StringComparison]::Ordinal)) { return $line }
            }
        } catch [TimeoutException] {}
    }
    throw 'Timed out waiting for Tab5 response. Confirm the app firmware is running and retry.'
}

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.WriteLine("SD_PUT_BEGIN $($file.Length) $sha $pathHex")
    $ready = Wait-Tab5Line @('SD_PUT_READY', 'SD_PUT_ERROR') 15
    if ($ready.StartsWith('SD_PUT_ERROR')) { throw $ready }
    if ($ready -notmatch 'chunk=(\d+)') { throw "Invalid ready response: $ready" }
    $chunkBytes = [int]$Matches[1]
    $buffer = [byte[]]::new($chunkBytes)
    $stream = [IO.File]::OpenRead($source)
    try {
        $sent = 0L
        while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $serial.WriteLine("SD_PUT_CHUNK $count")
            $dataReady = Wait-Tab5Line @('SD_PUT_DATA', 'SD_PUT_ERROR') 15
            if ($dataReady.StartsWith('SD_PUT_ERROR')) { throw $dataReady }
            $serial.Write($buffer, 0, $count)
            $ack = Wait-Tab5Line @('SD_PUT_ACK', 'SD_PUT_ERROR') 15
            if ($ack.StartsWith('SD_PUT_ERROR')) { throw $ack }
            $sent += $count
            Write-Progress -Activity 'Copying to Tab5 microSD' -Status "$sent / $($file.Length) bytes" `
                -PercentComplete (($sent * 100.0) / $file.Length)
        }
    } finally {
        $stream.Dispose()
    }
    $done = Wait-Tab5Line @('SD_PUT_DONE', 'SD_PUT_ERROR') 30
    if ($done.StartsWith('SD_PUT_ERROR')) { throw $done }
    if ($done -notmatch "sha256=$sha") { throw "Tab5 returned the wrong digest: $done" }
    Write-Progress -Activity 'Copying to Tab5 microSD' -Completed
    Write-Output $done
} catch {
    if ($serial.IsOpen) {
        try { $serial.WriteLine('SD_PUT_ABORT') } catch {}
    }
    throw
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
