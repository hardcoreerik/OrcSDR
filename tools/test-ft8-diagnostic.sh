#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
ui=apps/orcsdr-tab5/ui
sources=()
for name in ft8_native_backend ft8_spectral_fft ft8_mode ft8_sync ft8_demod ft8_pipeline ft8_snr ft8_message ft8_codec ft8_ldpc ft8_ldpc_decode ft8_model; do sources+=("$ui/$name.cpp"); done
common=(-std=c++17 -Wall -Wextra -Werror -pedantic -I"$ui" -Itools)
for variant in production diagnostic sanitized; do
  flags=(-O2)
  if [[ $variant != production ]]; then flags+=(-DORCSDR_FT8_DIAG); fi
  if [[ $variant == sanitized ]]; then flags+=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer); fi
  g++ "${common[@]}" "${flags[@]}" tests/ft8_native_backend_tests.cpp "${sources[@]}" -o "$build/backend-$variant"
  "$build/backend-$variant"
  g++ "${common[@]}" "${flags[@]}" tests/ft8_pipeline_tests.cpp "$ui/ft8_spectral.cpp" "${sources[@]}" -o "$build/pipeline-$variant"
  "$build/pipeline-$variant"
done
g++ "${common[@]}" -O2 -DORCSDR_FT8_DIAG tools/ft8-native-diagnose.cpp "${sources[@]}" -o "$build/diagnose"
python3 - "$build" <<'PY'
import pathlib, sys, wave
p=pathlib.Path(sys.argv[1])
with wave.open(str(p/'quiet.wav'), 'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(12000); w.writeframes(bytes(360000))
(p/'quiet.ref').write_text('1000 0.0 CQ "CONTROL"\n')
PY
"$build/diagnose" "$build/quiet.wav" ft8 --ref "$build/quiet.ref" --json > "$build/result.json"
python3 - "$build/result.json" <<'PY'
import json, sys
r=json.load(open(sys.argv[1]))
assert r['decodes']==0 and r['reference_decoded']==0
assert r['references'][0]['message']=='CQ "CONTROL"'
PY
for args in 'badmode --ref none' 'ft8 --ref none --lead-ms -1' 'ft8 --ref none --k 65'; do
  if "$build/diagnose" "$build/quiet.wav" $args; then echo 'invalid arguments accepted' >&2; exit 1; else test $? -eq 2; fi
done
echo 'FT8/FT4 diagnostic integration: PASS'
