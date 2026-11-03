param(
  [string]$SupportRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path,
  [string]$Image = 'emscripten/emsdk:3.1.74'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$support = (Resolve-Path $SupportRepo).Path
$shellCommand = @'
set -eu
emcc -O2 -std=c++17 -I/repo/apps/orcsdr-tab5/ui \
  -I/support/apps/orcsdr-web/decoders/shim_portable \
  -I/support/apps/orcsdr-web/decoders/shim_host \
  -include /support/apps/orcsdr-web/decoders/shim_host/orcdec_compat.h \
  /repo/apps/orcsdr-tab5/ui/lora_native_decoder.cpp \
  /repo/apps/orcsdr-tab5/tests/lora_native_payload_test.cpp \
  -sEXIT_RUNTIME=1 -o /tmp/lora_native_payload_test.js
node /tmp/lora_native_payload_test.js
'@
$shellCommand = $shellCommand -replace "`r`n", "`n"
$shellCommand = $shellCommand -replace "`r", ''
$dockerArgs = @(
  'run', '--rm',
  '--mount', "type=bind,source=$repo,target=/repo,readonly",
  '--mount', "type=bind,source=$support,target=/support,readonly",
  '--workdir', '/repo', $Image,
  'bash', '-lc', $shellCommand
)
& docker @dockerArgs
if ($LASTEXITCODE -ne 0) { throw "LoRa payload WASM test failed ($LASTEXITCODE)." }
