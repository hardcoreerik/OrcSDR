#!/usr/bin/env bash
cd "$(dirname "$0")"
R=../../../build/dv-live-rel/dv_replay
for f in 454.1250 454.1500 454.0250 453.7250 454.5750; do
  hz=$(python3 -c "print(int(round($f*1e6)))"); n=cap_wide_${f/./p}.cu8
  echo "=== $f MHz (wide capture, DDC)"; $R $n $hz 2>&1 | grep -E "ev=|RTL_DV_STATUS|shown" | cut -c1-330 | tail -8
done
