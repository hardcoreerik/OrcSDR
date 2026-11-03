param(
  [string]$SourceRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path,
  [string]$Image = 'emscripten/emsdk:3.1.74'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$source = (Resolve-Path $SourceRepo).Path
$shellCommand = @'
set -eu
g++ -std=c++17 -O1 -Wall -Wextra \
  -I/repo/apps/orcsdr-tab5/tests/packet_radio_fakes \
  -I/source/apps/orcsdr-p4-hosyond-console/main \
  -I/source/apps/orcsdr-tab5/ui \
  -I/source/ports/p4/components/orcsdr_board_hosyond/include \
  -I/source/ports/p4/components/orcsdr_sx126x/include \
  /source/apps/orcsdr-p4-hosyond-console/main/packet_radio_service.cpp \
  /repo/apps/orcsdr-tab5/tests/packet_radio_service_host_test.cpp \
  -o /tmp/packet_radio_service_host_test
for scenario in startup cancel_retry current overflow stale; do
  /tmp/packet_radio_service_host_test "$scenario"
  echo "PACKET_RADIO_HOST_TEST pass=$scenario"
done
'@
$shellCommand = $shellCommand -replace "`r`n", "`n"
$shellCommand = $shellCommand -replace "`r", ''
$dockerArgs = @(
  'run', '--rm',
  '--mount', "type=bind,source=$repo,target=/repo,readonly",
  '--mount', "type=bind,source=$source,target=/source,readonly",
  '--workdir', '/repo', $Image,
  'bash', '-lc', $shellCommand
)
& docker @dockerArgs
if ($LASTEXITCODE -ne 0) { throw "Packet-radio host tests failed ($LASTEXITCODE)." }
