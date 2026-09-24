#requires -Version 5.1
<#
Returns the path of the pinned ESP-Hosted C6 image for embedding in developer
builds, building it once into a per-user cache shared by every checkout.
Release packaging builds its own copy (tools/release/build-m5burner.ps1).
A cached image is reused only when its provenance matches the pinned revision
in tools/release/hosted-c6-release.json and its SHA-256 still matches.
#>
param(
  [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.5.4'
)

$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$releaseTools = Join-Path $repo 'tools\release'
$lock = Get-Content (Join-Path $releaseTools 'hosted-c6-release.json') -Raw | ConvertFrom-Json
# Cache beside the main checkout's .git so every worktree shares one build.
# Not %LOCALAPPDATA%: packaged apps can redirect it, and CMake then sees two
# different paths for the same tree. Keep the path short for Windows MAX_PATH.
$commonGitDir = (git -C $repo rev-parse --path-format=absolute --git-common-dir).Trim()
if ($LASTEXITCODE -ne 0 -or -not $commonGitDir) { throw "Could not locate the git directory for $repo." }
$cacheRoot = Join-Path (Split-Path $commonGitDir -Parent) '.orcsdr-cache\hosted-c6'
$cacheDir = Join-Path $cacheRoot ("{0}-{1}" -f $lock.hosted_version, $lock.source_revision.Substring(0, 12))
$image = Join-Path $cacheDir $lock.output_name
$provenancePath = Join-Path $cacheDir 'c6-provenance.json'

function Test-CachedImage {
  if (-not (Test-Path -LiteralPath $image -PathType Leaf) -or
      -not (Test-Path -LiteralPath $provenancePath -PathType Leaf)) { return $false }
  $provenance = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
  $hash = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
  return $provenance.source_revision -eq $lock.source_revision -and
         $provenance.hosted_version -eq $lock.hosted_version -and
         $provenance.sha256 -eq $hash
}

if (-not (Test-CachedImage)) {
  Write-Host "Building pinned ESP-Hosted $($lock.hosted_version) C6 image into $cacheDir (one time)..."
  # Keep the ESP-Hosted clone beside the cache instead of %TEMP%, where
  # cleanup can leave a partial .git behind.
  & (Join-Path $releaseTools 'build-hosted-c6.ps1') -OutputDirectory $cacheDir -IdfPath $IdfPath `
      -SourceDirectory (Join-Path $cacheRoot 'src') | Out-Host
  if (-not (Test-CachedImage)) { throw "ESP-Hosted C6 image could not be built or verified in $cacheDir." }
}
Write-Host "Embedding ESP-Hosted $($lock.hosted_version) C6 image: $image"
return $image
