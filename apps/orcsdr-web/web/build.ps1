# Builds the shared OrcSDR screens and decoders to WebAssembly in the emscripten Docker image, then packs the page assets into
# apps/orcsdr-web/dist/*.gz (the directory apps/orcsdr-p4-core embeds by default). Needs Docker and git.
# Everything comes from this repository except M5GFX, which is fetched at a pinned tag and commit (see ../THIRD_PARTY_NOTICES.md).
# The dashboard and decoder sources are apps/orcsdr-tab5/ui, compiled in place: the same files the devices build.
param([string]$Image = 'emscripten/emsdk:3.1.74')
$ErrorActionPreference = 'Stop'
$M5GFXTag = '0.2.25'
$M5GFXCommit = 'ad9b814264d4e2000e9f30070002310bbccaffc9'
$web = $PSScriptRoot
$repo = (Resolve-Path "$web\..\..\..").Path
$m5gfx = "$web\third_party\M5GFX"
if (-not (Test-Path "$m5gfx\src\M5GFX.h")) {
    New-Item -ItemType Directory -Force "$web\third_party" | Out-Null
    git clone --depth 1 --branch $M5GFXTag https://github.com/m5stack/M5GFX.git $m5gfx
    if ($LASTEXITCODE) { throw 'M5GFX clone failed' }
}
$have = (git -C $m5gfx rev-parse HEAD).Trim()
if ($have -ne $M5GFXCommit) { throw "M5GFX is at $have, expected $M5GFXCommit ($M5GFXTag)" }

docker run --rm -v "${repo}:/repo" $Image sh /repo/apps/orcsdr-web/web/build.sh
if ($LASTEXITCODE) { throw 'emcc build failed' }

docker run --rm -v "${repo}:/repo" $Image sh /repo/apps/orcsdr-web/decoders/wasm/build.sh
if ($LASTEXITCODE) { throw 'orcdec wasm build failed' }

& "$web\pack.ps1"
