# Packs the page assets into apps/orcsdr-web/dist/ without rebuilding the WebAssembly (use after editing only the .js/.html under
# apps/orcsdr-web/web/app). build.ps1 runs this after rebuilding core.wasm and orcdec.wasm.
$ErrorActionPreference = 'Stop'
$web = $PSScriptRoot
$repo = (Resolve-Path "$web\..\..\..").Path
function Pack($src, $dst) {
    $in = [IO.File]::ReadAllBytes($src)
    $fs = [IO.File]::Create($dst)
    $gz = New-Object IO.Compression.GZipStream($fs, [IO.Compression.CompressionLevel]::SmallestSize)
    $gz.Write($in, 0, $in.Length); $gz.Dispose(); $fs.Dispose()
    '{0,-18} {1,8} -> {2,8} B gzip' -f (Split-Path $src -Leaf), $in.Length, (Get-Item $dst).Length
}
$out = "$repo\apps\orcsdr-web\dist"
New-Item -ItemType Directory -Force $out | Out-Null
Pack "$web\app\m5.html" "$out\m5.html.gz"
Pack "$web\app\app.js" "$out\app.js.gz"
Pack "$web\app\decoders.js" "$out\decoders.js.gz"
Pack "$web\app\decoder_worker.js" "$out\decoder_worker.js.gz"
Pack "$web\dist\core.js" "$out\core.js.gz"
Pack "$web\dist\core.wasm" "$out\core.wasm.gz"
Pack "$repo\apps\orcsdr-web\decoders\build\wasm\orcdec.js" "$out\orcdec.js.gz"
Pack "$repo\apps\orcsdr-web\decoders\build\wasm\orcdec.wasm" "$out\orcdec.wasm.gz"
Copy-Item "$web\app\splash.jpg" "$out\splash.jpg" -Force
