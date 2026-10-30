#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
# Test project-native decoder against synthetic IQ, plus optional real fixture.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
LOG="$(pwd)/vdl2-native-host.log"
CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic -fno-omit-frame-pointer)
if [[ "${VDL2_SANITIZE:-1}" == 1 ]]; then FLAGS+=(-fsanitize=address,undefined); fi
INC=(-I"$ROOT/apps/orcsdr-tab5/ui")
SRC=("$ROOT/apps/orcsdr-tab5/ui/vdl2_channel.cpp" "$ROOT/apps/orcsdr-tab5/ui/vdl2_receiver.cpp" "$ROOT/apps/orcsdr-tab5/ui/vdl2_rs.cpp" "$ROOT/apps/orcsdr-tab5/ui/vdl2_burst.cpp" "$ROOT/apps/orcsdr-tab5/ui/vdl2_frames.cpp")
{
 echo "VDL2 native host test UTC: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
 "$CXX" "${FLAGS[@]}" "${INC[@]}" "$ROOT/tests/vdl2_rs_test.cpp" "$ROOT/apps/orcsdr-tab5/ui/vdl2_rs.cpp" "$ROOT/apps/orcsdr-tab5/ui/vdl2_burst.cpp" -o "$TMP/rs"
 "$TMP/rs"
 "$CXX" "${FLAGS[@]}" "${INC[@]}" "$ROOT/tests/vdl2_receiver_test.cpp" "${SRC[@]}" -o "$TMP/receiver"
 "$TMP/receiver"
 "$CXX" "${FLAGS[@]}" "${INC[@]}" "$ROOT/tests/vdl2_channel_test.cpp" "${SRC[@]}" -o "$TMP/channel"
 "$TMP/channel"
 if python3 -c 'import numpy, scipy' 2>/dev/null; then
   python3 "$ROOT/tests/test_vdl2_prepare_iq.py"      # the converter's own regression test (needs numpy and scipy)
 else
   echo 'tests/test_vdl2_prepare_iq.py: NOT RUN (numpy/scipy missing)'
 fi
 "$CXX" "${FLAGS[@]}" "${INC[@]}" "$ROOT/tools/vdl2_replay_iq.cpp" "${SRC[@]}" -o "$TMP/replay"
 if [[ $# -gt 0 ]]; then
   echo "REAL OFF-AIR FIXTURE: $1"
   "$TMP/replay" "$1" i16 50000 | tee "$TMP/replay.log"
   grep -Eq '^ACARS tail=\.*HB-IJW[ ]' "$TMP/replay.log" || { echo 'FAILED real fixture: expected HB-IJW'; exit 1; }
   awk '/VDL2_REPORT/ {for(i=1;i<=NF;i++)if($i~/^frames=/){split($i,a,"=");if(a[2]>=2)ok=1}}END{exit !ok}' "$TMP/replay.log" || { echo 'FAILED real fixture: <2 AVLC frames'; exit 1; }
   echo 'REAL IQ: ACCEPTANCE PASSED (2+ AVLC and HB-IJW)'
 else
   echo 'REAL IQ: NOT RUN (pass xng vdl2_offair_6s.i16 as first argument)'
 fi
 echo 'All executed VDL2 tests PASS'
} 2>&1 | tee "$LOG"
echo "Log saved: $LOG"
