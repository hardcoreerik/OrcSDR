#!/usr/bin/env bash
cd "$(dirname "$0")"
R=../../../build/dv-live-rel/dv_replay
run() { echo "=== $1 MHz ($3)"; $R "$2" "$4" $5 2>&1 | tail -${6:-14}; }
run 152.1575 cap_152p1575_g30.cu8 DMR-T3-ctrl 152157500
run 454.225  cap_454p225_g30.cu8  NXDN-ctrl 454225000
run 454.350  cap_454p350_g30.cu8  NXDN-ctrl 454350000
run 453.950  cap_453p950_g30.cu8  UO-NXDN-ctrl 453950000
run 454.2125 cap_454p2125_g30.cu8 NXDN-ctrl 454212500
