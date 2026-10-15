#!/usr/bin/env bash
# Build and run the SSTV host regressions (Linux, macOS or WSL).
#
#   tools/sstv_host_tests.sh                       # all synthetic tests
#   SSTV_FIXTURE_WAV=/path/received.wav tools/sstv_host_tests.sh   # + real-WAV replay
#
# Every build and run writes a timestamped log into the directory the script is
# launched from, plus one summary file with exit codes, the fixture SHA256 and
# the inputs. Exit status is non-zero if any step fails.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$PWD"
STAMP="$(date +%Y%m%d-%H%M%S)"
BIN="${TMPDIR:-/tmp}/sstv-host-$STAMP"
SUMMARY="$OUT/sstv-summary-$STAMP.log"
CXXFLAGS=(-std=c++17 -O2 -Wall -Wextra -DORCSDR_SSTV_TESTING -I"$ROOT/apps/orcsdr-tab5/ui" -I"$ROOT/apps/orcsdr-tab5/tests")
UI="$ROOT/apps/orcsdr-tab5/ui"
TESTS="$ROOT/apps/orcsdr-tab5/tests"
CORE=("$UI/sstv_receiver.cpp" "$UI/sstv_modes.cpp" "$UI/sstv_auto_detect.cpp")
mkdir -p "$BIN"
status=0

{
  echo "SSTV host regression  $(date -Is)"
  echo "repo:    $ROOT"
  # SSTV_COMMIT lets a caller supply it where git cannot read the worktree (for example WSL on a Windows checkout).
  echo "commit:  ${SSTV_COMMIT:-$(git -c safe.directory='*' -C "$ROOT" rev-parse --short HEAD 2>/dev/null)} ($(git -c safe.directory='*' -C "$ROOT" branch --show-current 2>/dev/null))"
  echo "compiler: $(g++ --version | head -1)"
  echo "flags:   ${CXXFLAGS[*]}"
} | tee "$SUMMARY"

build_and_run() {
  local name="$1"; shift
  local log_build="$OUT/sstv-$name-build-$STAMP.log" log_run="$OUT/sstv-$name-run-$STAMP.log"
  g++ "${CXXFLAGS[@]}" "$@" -o "$BIN/$name" >"$log_build" 2>&1
  local b=$?
  local warnings; warnings=$(grep -c 'warning:' "$log_build")
  if [ $b -ne 0 ]; then
    echo "$name: BUILD FAILED (see $log_build)" | tee -a "$SUMMARY"; status=1; return
  fi
  "$BIN/$name" >"$log_run" 2>&1
  local r=$?
  echo "$name: build ok (warnings=$warnings) run exit=$r  logs: $(basename "$log_build") $(basename "$log_run")" | tee -a "$SUMMARY"
  [ $r -ne 0 ] && { status=1; tail -3 "$log_run" | sed 's/^/    /' | tee -a "$SUMMARY"; }
  return 0
}

build_and_run autodetect "$TESTS/sstv_auto_detect_test.cpp" "$UI/sstv_auto_detect.cpp" "$UI/sstv_modes.cpp"
build_and_run smoke      "$TESTS/sstv_receiver_smoke.cpp" "${CORE[@]}"
build_and_run autostate  "$TESTS/sstv_auto_state_test.cpp" "${CORE[@]}"
build_and_run archive    "$TESTS/sstv_archive_test.cpp" "$UI/sstv_archive.cpp" "${CORE[@]}" -pthread
build_and_run status     "$TESTS/sstv_status_test.cpp" "$UI/sstv_status.cpp" "${CORE[@]}"
build_and_run settings   "$TESTS/sstv_settings_test.cpp" "$UI/sstv_settings.cpp" "${CORE[@]}"
build_and_run noise      "$TESTS/sstv_noise_test.cpp" "${CORE[@]}"
build_and_run tune       "$TESTS/sstv_tune_test.cpp" "$UI/sstv_tune.cpp" "$UI/sstv_audio_scope.cpp"
build_and_run syncmeter  "$TESTS/sstv_syncmeter_test.cpp" "$UI/sstv_tune.cpp" "${CORE[@]}"
build_and_run capture   "$TESTS/sstv_capture_test.cpp" "$UI/sstv_capture.cpp"
build_and_run memory    "$TESTS/sstv_memory_test.cpp" "${CORE[@]}"
build_and_run export    "$TESTS/sstv_export_test.cpp" "$UI/sstv_export.cpp"
build_and_run reclaim   "$TESTS/memory_reclaim_test.cpp" "$UI/memory_reclaim.cpp"
SSTV_PATTERN_RGB="$ROOT/apps/orcsdr-tab5/main/sstv_testpattern_320x240.rgb" build_and_run colour "$TESTS/sstv_colour_test.cpp" "${CORE[@]}"
SSTV_PATTERN="$ROOT/apps/orcsdr-tab5/main/sstv_testpattern_320x240.gray" build_and_run pattern "$TESTS/sstv_pattern_test.cpp" "${CORE[@]}"

if [ -n "${SSTV_FIXTURE_WAV:-}" ]; then
  EXPECT_VIS="${SSTV_EXPECT_VIS:-8}"; EXPECT_ROWS="${SSTV_EXPECT_ROWS:-239}"
  g++ "${CXXFLAGS[@]}" "$TESTS/sstv_wav_replay.cpp" "${CORE[@]}" -o "$BIN/replay" >"$OUT/sstv-replay-build-$STAMP.log" 2>&1 || { echo "replay: BUILD FAILED" | tee -a "$SUMMARY"; status=1; }
  if [ -x "$BIN/replay" ]; then
    {
      echo "fixture:  $SSTV_FIXTURE_WAV"
      if command -v sha256sum >/dev/null 2>&1; then echo "sha256:   $(sha256sum "$SSTV_FIXTURE_WAV" | cut -c1-64)";
      elif command -v shasum >/dev/null 2>&1; then echo "sha256:   $(shasum -a 256 "$SSTV_FIXTURE_WAV" | cut -c1-64)";
      else echo "sha256:   UNAVAILABLE (no sha256sum or shasum)"; status=1; fi
      echo "size:     $(stat -c %s "$SSTV_FIXTURE_WAV" 2>/dev/null || stat -f %z "$SSTV_FIXTURE_WAV") bytes"
      echo "expected: VIS=$EXPECT_VIS rows>=$EXPECT_ROWS"
    } | tee -a "$SUMMARY"
    "$BIN/replay" "$SSTV_FIXTURE_WAV" "$EXPECT_VIS" "$EXPECT_ROWS" >"$OUT/sstv-replay-run-$STAMP.log" 2>&1
    r=$?
    echo "replay: exit=$r  $(cat "$OUT/sstv-replay-run-$STAMP.log" | tail -1)" | tee -a "$SUMMARY"
    [ $r -ne 0 ] && status=1
  fi
else
  echo "replay: SKIPPED (set SSTV_FIXTURE_WAV to a 48 kHz mono PCM16 WAV)" | tee -a "$SUMMARY"
fi

echo "overall: $([ $status -eq 0 ] && echo PASS || echo FAIL)  summary: $SUMMARY" | tee -a "$SUMMARY"
exit $status
