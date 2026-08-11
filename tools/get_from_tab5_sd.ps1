param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$SourcePath,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$Destination,

    [string]$Port = 'COM17'
)

$ErrorActionPreference = 'Stop'
if ($SourcePath -notmatch '^/orcsdr/[\x20-\x7e]+$' -or $SourcePath.Contains('..')) {
    throw 'SourcePath must be an ASCII path below /orcsdr/ without ..'
}
$pathHex = [Convert]::ToHexString([Text.Encoding]::ASCII.GetBytes($SourcePath)).ToLowerInvariant()

$serial = [IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.NewLine = "`n"
$serial.ReadTimeout = 2000
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

function Read-ExactBytes([int]$Count) {
    $buf = [byte[]]::new($Count)
    $offset = 0
    while ($offset -lt $Count) {
        $n = $serial.Read($buf, $offset, $Count - $offset)
        if ($n -le 0) { throw 'Serial read returned no data.' }
        $offset += $n
    }
    return $buf
}

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $serial.WriteLine("SD_GET_BEGIN $pathHex")
    $ready = Wait-Tab5Line @('SD_GET_READY', 'SD_GET_ERROR')
    if ($ready.StartsWith('SD_GET_ERROR')) { throw $ready }
    if ($ready -notmatch 'bytes=(\d+)') { throw "Invalid ready response: $ready" }
    $total = [long]$Matches[1]

    $out = [IO.File]::Create($Destination)
    try {
        $got = 0L
        while ($got -lt $total) {
            $serial.WriteLine('SD_GET_CHUNK')
            $data = Wait-Tab5Line @('SD_GET_DATA', 'SD_GET_ERROR')
            if ($data.StartsWith('SD_GET_ERROR')) { throw $data }
            if ($data -notmatch 'bytes=(\d+)') { throw "Invalid data response: $data" }
            $n = [int]$Matches[1]
            $bytes = Read-ExactBytes $n
            $out.Write($bytes, 0, $n)
            $got += $n
            Write-Progress -Activity 'Copying from Tab5 microSD' -Status "$got / $total bytes" `
                -PercentComplete (($got * 100.0) / $total)
        }
    } finally {
        $out.Dispose()
    }
    $done = Wait-Tab5Line @('SD_GET_DONE', 'SD_GET_ERROR') 30
    if ($done.StartsWith('SD_GET_ERROR')) { throw $done }
    Write-Progress -Activity 'Copying from Tab5 microSD' -Completed
    Write-Output $done
} catch {
    if ($serial.IsOpen) {
        try { $serial.WriteLine('SD_GET_ABORT') } catch {}
    }
    throw
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
