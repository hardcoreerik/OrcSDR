#!/usr/bin/env bash
# Usage: run_host_suites.sh <Release|San>; prints one PASS/FAIL line per suite.
cfg=$1; cd "$(dirname "$0")/../.."
if [ "$cfg" = San ]; then T="-DCMAKE_BUILD_TYPE=Debug"; else T="-DCMAKE_BUILD_TYPE=Release"; fi
declare -A SAN=( [ui]=-DORC_DV_UI_SANITIZERS=ON [dmr]=-DORCSDR_DMR_SANITIZE=ON )
suites="ui:tests/digital_voice_dashboard_preview dmr:components/orcsdr_dmr dmr_d1_integration:components/orcsdr_dmr/d1_integration dmr_conf:tests/dmr_protocol_conformance dvrx:components/orcsdr_digital_voice_rx nxdn:components/orcsdr_nxdn discovery:components/orcsdr_radio_discovery"
for s in $suites; do n=${s%%:*}; d=${s#*:}; b=build/hs-$cfg-$n; extra=""
  [ "$cfg" = San ] && extra="${SAN[$n]:--DCMAKE_CXX_FLAGS=-fsanitize=address,undefined}"
  rm -rf $b; log=artifacts/digital-voice-integration/logs/host_${cfg}_$n.log
  if cmake -S $d -B $b $T $extra >$log 2>&1 && cmake --build $b --parallel 4 >>$log 2>&1 && ctest --test-dir $b --output-on-failure >>$log 2>&1; then
    echo "PASS $cfg $n: $(grep -E 'tests passed' $log | tail -1)"; else echo "FAIL $cfg $n (see $log)"; fi
done
