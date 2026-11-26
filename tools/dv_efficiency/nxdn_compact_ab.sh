#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
log="$PWD/nxdn-compact-ab.log"
if [ "$#" -gt 0 ]; then log="$1"; fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
n="$root/components/orcsdr_nxdn"
u="$root/apps/orcsdr-tab5/ui"
{
  c++ -std=c++17 -O3 -I"$n/include" -I"$u" "$root/tools/dv_efficiency/nxdn_compact_ab.cpp" "$n/src/frontend.cpp" "$u/dv_channelizer.cpp" -o "$tmp/ab"
  for mode in 48 96; do
    for file in noise analog "$root"/tests/digital_voice_corpus/iq/*.cu8; do "$tmp/ab" "$file" "$mode"; done
  done
  # Original published IQ only in temp. Never export IQ, audio or bit streams.
  upstream=https://raw.githubusercontent.com/MattCheramie/GopherTrunk/f06bfff493f47fdeb69807c8b08b2b32a253839f/internal/radio/nxdn/testdata
  run_fixture() {
    local mode="$1" file="$2" expected="$3"
    curl -fsSL --retry 3 "$upstream/$file" -o "$tmp/$file"
    test "$(git hash-object "$tmp/$file")" = "$expected"
    "$tmp/ab" "$tmp/$file" "$mode"
  }
  run_fixture 48 nxdn48_rdch_voice_447100_16oct_48k.cs16 26aed395774380be0cf6a0ebdf391723a9335e00
  run_fixture 96 nxdn96_rdch_voice_447100_15oct_48k.cs16 01845523c82df3a279712b577a0f0180653f981f
  run_fixture 96 nxdn96_rcch_cc_453775_19oct_48k.cs16 c4a1bdb2fdd36cc316877982781c99bc7043462d
} | tee "$log"
