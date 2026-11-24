#!/bin/sh
# Stage B impairment matrix: real call capture + controlled impairments through the production chain. Output one line per case.
# usage: tools/dv_voice_impairments.sh <dv_voice_replay> <capture.cu8> <channel_hz> <outdir>
B=$1; C=$2; HZ=$3; O=$4; mkdir -p "$O"
run() { name=$1; shift; printf '%s: ' "$name"; "$B" "$C" "$HZ" "$O" "imp_$name" 100 "$@" | head -2 | tr '\n' ' ' | sed 's/engine groups/| engine groups/'; echo; }
run clean
run offset_p1500 offset_hz=1500
run offset_m1500 offset_hz=-1500
run offset_p3000 offset_hz=3000
run noise_20db noise_db=20
run noise_12db noise_db=12
run noise_8db noise_db=8
run drift_p50 drift_ppm=50
run drift_p200 drift_ppm=200
run drift_m100 drift_ppm=-100
run burstloss_100ms zero_at_s=3.0 zero_ms=100
run burstloss_500ms zero_at_s=3.0 zero_ms=500
run splice_37ms cut_at_s=3.0 cut_ms=37
run splice_300ms cut_at_s=3.0 cut_ms=300
