#requires -Version 5.1
<#
Returns the path of the pinned OrcMaps world pack that is flashed into the
read-only `orcmaps` partition (the first-run location picker's basemap).

The pack is an OrcMaps release artifact, not an OrcSDR asset: OrcSDR only pins
it by version and SHA-256. It is cached beside the main checkout
(.orcsdr-cache/orcmaps-world/, shared by every worktree) and is taken, in
order, from an explicit -WorldPack path, the cache, the OrcMaps GitHub release,
or a local OrcMaps checkout's build output. Only the exact pinned bytes are
accepted from any source.
#>
param(
  [string]$WorldPack
)

$ErrorActionPreference = 'Stop'

# The world pack published with OrcMaps v0.2.0; unchanged in v0.2.1, which
# main/idf_component.yml pins the engine to.
$version = '0.2.0'
$fileName = "orcmaps-world-z4-$version.pmtiles"
$expectedSha256 = '9aea08772bacf1f024d1da90cc52aa8fcf0b0e37405415dc7c91e75e56596f0f'
$expectedBytes = 871343
$releaseUrl = "https://github.com/hardcoreerik/orcmaps/releases/download/v$version/$fileName"

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$commonGitDir = (git -C $repo rev-parse --path-format=absolute --git-common-dir).Trim()
if ($LASTEXITCODE -ne 0 -or -not $commonGitDir) { throw "Could not locate the git directory for $repo." }
$mainCheckout = Split-Path $commonGitDir -Parent
$cacheDir = Join-Path $mainCheckout ".orcsdr-cache\orcmaps-world\$version"
$cached = Join-Path $cacheDir $fileName

function Test-WorldPack([string]$Path) {
  if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
  if ((Get-Item -LiteralPath $Path).Length -ne $expectedBytes) { return $false }
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expectedSha256
}

function Save-ToCache([string]$Path) {
  New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
  Copy-Item -LiteralPath $Path -Destination $cached -Force
}

if ($WorldPack) {
  if (-not (Test-WorldPack $WorldPack)) {
    throw "World pack $WorldPack is not OrcMaps $version (expected $expectedBytes bytes, SHA-256 $expectedSha256)."
  }
  Save-ToCache $WorldPack
} elseif (-not (Test-WorldPack $cached)) {
  $found = $false
  try {
    New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
    $download = "$cached.download"
    Invoke-WebRequest -Uri $releaseUrl -OutFile $download -UseBasicParsing
    if (Test-WorldPack $download) { Move-Item -LiteralPath $download -Destination $cached -Force; $found = $true }
    else { Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue }
  } catch {
    Remove-Item -LiteralPath "$cached.download" -Force -ErrorAction SilentlyContinue
  }
  if (-not $found) {
    # A local OrcMaps checkout beside the main OrcSDR checkout, built with its
    # tools/pack-builder/build_world_overview.py (the release pack reproduces
    # byte-for-byte from it).
    $local = Join-Path (Split-Path $mainCheckout -Parent) 'OrcMaps\data\local\world-overview\build\world-overview-z4.pmtiles'
    if (Test-WorldPack $local) { Save-ToCache $local; $found = $true }
  }
  if (-not $found) {
    throw ("OrcMaps world pack $version not found. Download $fileName from $releaseUrl " +
           "and pass -WorldPack <path>, or build it with OrcMaps tools/pack-builder, " +
           "or build with -WithoutWorldPack (first-run location falls back to no map).")
  }
}
Write-Host "Flashing OrcMaps $version world pack into the orcmaps partition: $cached"
return $cached
