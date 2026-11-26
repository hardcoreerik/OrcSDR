#!/usr/bin/env bash
set -euo pipefail
base=$(cd "$1" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)
log="$PWD/synth-ab.log"
if [ "$#" -gt 1 ]; then log="$2"; fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
{
  for r in "$base" "$root"; do
    if [ "$r" = "$base" ]; then name=base; else name=candidate; fi
    mbe="$r/components/mbelib"
    cc -std=gnu99 -O3 -fstack-usage -I"$mbe" -c "$mbe/mbelib.c" -o "$tmp/$name.o"
    cc -std=gnu99 -O3 -I"$mbe" "$root/tools/dv_efficiency/synth_ab.c" "$tmp/$name.o" -lm -o "$tmp/$name"
    "$tmp/$name" "$tmp/$name.bin"
    grep synthesizeSpeechf "$tmp/$name.su"
  done
  cmp "$tmp/base.bin" "$tmp/candidate.bin"
  sha256sum "$tmp/base.bin" "$tmp/candidate.bin"
  echo "PASS: shared raw float synthesis, state and subsequent RNG identical on 768 synthetic bounded frames"
} | tee "$log"
