#!/usr/bin/env bash
# Host benchmarks on the real-capture corpus. Weak signal = added white Gaussian noise relative to the capture's own RMS.
# False identification = pure noise through all decoders. Output: JSON lines in this directory.
set -u
cd "$(dirname "$0")/../../.."
B=build/dv-live-rel/dv_bench; C=tests/digital_voice_corpus/iq; O=artifacts/digital-voice-integration/benchmarks
: > $O/weak_signal.jsonl; : > $O/false_id.jsonl
for f in "terra_152p1575_call_cc13.cu8 152157500" "terra_152p685_call_cc8.cu8 152685000" "terra_152p1575_control_only.cu8 152157500"; do
  set -- $f
  for db in -999 -18 -15 -12 -9 -6 -3 0; do
    for seed in 1 2 3; do
      [ "$db" = -999 ] && [ $seed -gt 1 ] && continue
      echo "{\"file\":\"$1\",\"result\":$($B file $C/$1 $2 1 $db $seed)}" >> $O/weak_signal.jsonl
    done
  done
done
for mask in 1 2 4 7; do
  for seed in 1 2; do
    echo "{\"mask\":$mask,\"seed\":$seed,\"result\":$($B noise 900 $mask 25 $seed)}" >> $O/false_id.jsonl
  done
done
echo done > $O/host_benchmarks.done
