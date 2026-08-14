[CmdletBinding()]
param(
    [string]$Port = 'COM17',
    [Parameter(Mandatory = $true)][string]$Release,
    [switch]$All,
    [switch]$SkipCapture,
    [switch]$ApproveVoice
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$venv = Join-Path $repo '.local\help-media-venv'
$python = Join-Path $venv 'Scripts\python.exe'
$requirements = Join-Path $PSScriptRoot 'requirements-help-media.txt'
$requirementsHash = (Get-FileHash -LiteralPath $requirements -Algorithm SHA256).Hash
$marker = Join-Path $venv "requirements-$requirementsHash.ok"
$manifest = Join-Path $repo 'docs\help_media\manifest.json'
$output = Join-Path $repo "artifacts\help-media\$Release"

if (-not $All) {
    throw 'Specify -All. Partial media builds use tools/help_media.py directly.'
}

if (-not (Test-Path -LiteralPath $python)) {
    $bootstrap = 'C:\Users\hardc\AppData\Local\Programs\Python\Python311\python.exe'
    if (-not (Test-Path -LiteralPath $bootstrap)) { throw 'Python 3.11 was not found.' }
    & $bootstrap -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Python virtual environment creation failed.' }
}
if (-not (Test-Path -LiteralPath $marker)) {
    & $python -m pip install --disable-pip-version-check -r $requirements
    if ($LASTEXITCODE -ne 0) { throw 'Help-media dependency installation failed.' }
    New-Item -ItemType File -Path $marker -Force | Out-Null
}

$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) {
    $ffmpeg = 'C:\Users\hardc\AppData\Local\Microsoft\WinGet\Packages\yt-dlp.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-N-122319-gf6a95c7eb7-win64-gpl\bin\ffmpeg.exe'
}
if (-not (Test-Path -LiteralPath $ffmpeg)) { throw 'FFmpeg was not found.' }
$ffprobe = Join-Path (Split-Path -Parent $ffmpeg) 'ffprobe.exe'

& $python "$PSScriptRoot\help_media.py" validate --manifest $manifest --output $output
if ($LASTEXITCODE -ne 0) { throw 'Manifest validation failed.' }
if (-not $SkipCapture) {
    & $python "$PSScriptRoot\help_media.py" capture --manifest $manifest --output $output --port $Port --release $Release
    if ($LASTEXITCODE -ne 0) { throw 'Device capture failed.' }
}
& $python "$PSScriptRoot\help_media.py" annotate --manifest $manifest --output $output
if ($LASTEXITCODE -ne 0) { throw 'Image annotation failed.' }
& $python "$PSScriptRoot\help_media.py" catalog --manifest $manifest --output $output
if ($LASTEXITCODE -ne 0) { throw 'Guide catalog generation failed.' }
& $python -m mkdocs build --strict --config-file "$repo\mkdocs.yml"
if ($LASTEXITCODE -ne 0) { throw 'Strict MkDocs build failed.' }

$voiceSample = Join-Path $output 'kokoro-am-michael-sample.wav'
if (-not (Test-Path -LiteralPath $voiceSample)) {
    & $python "$PSScriptRoot\help_media.py" voice-sample --manifest $manifest --output $output
    if ($LASTEXITCODE -ne 0) { throw 'Voice sample generation failed.' }
}
if (-not $ApproveVoice) {
    Write-Host "VOICE APPROVAL REQUIRED: $voiceSample"
    Write-Host "Listen to the sample, then rerun the same command with -ApproveVoice."
    exit 2
}

& $python "$PSScriptRoot\help_media.py" video --manifest $manifest --output $output --ffmpeg $ffmpeg
if ($LASTEXITCODE -ne 0) { throw 'Video rendering failed.' }
Get-ChildItem -LiteralPath (Join-Path $output 'video') -Filter '*.mp4' | ForEach-Object {
    $probe = & $ffprobe -v error -show_entries 'stream=codec_name,width,height,r_frame_rate,sample_rate' -of json $_.FullName
    $metadata = $probe | ConvertFrom-Json
    $video = $metadata.streams | Where-Object { $_.codec_name -eq 'h264' } | Select-Object -First 1
    $audio = $metadata.streams | Where-Object { $_.codec_name -eq 'aac' } | Select-Object -First 1
    if (-not $video -or $video.width -ne 1920 -or $video.height -ne 1080 -or
        $video.r_frame_rate -ne '30/1' -or -not $audio) {
        throw "Media validation failed: $($_.FullName)"
    }
}
Write-Host "HELP_MEDIA_ALL_OK output=$output"
