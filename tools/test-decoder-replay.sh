#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

printf '%s\n' 'DECODER_REPLAY_CASE p25_iq status=RUN'
bash tools/test-p25-core.sh
printf '%s\n' 'DECODER_REPLAY_CASE p25_iq status=PASS input=tests/fixtures/p25_control_lane_453925.orciq format=ORCIQ'

wwv_fixture="tests/fixtures/wwv/wwv_10mhz_dial_1khz_below_15s.wav"
if [[ ! -f "$wwv_fixture" ]]; then
  printf '%s\n' "DECODER_REPLAY_CASE wwv_wav status=FAIL input=$wwv_fixture reason=missing_checked_in_fixture" >&2
  exit 1
fi

printf '%s\n' 'DECODER_REPLAY_CASE wwv_wav status=RUN'
g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -Iapps/orcsdr-tab5/ui \
  tests/wwv_tick_tests.cpp apps/orcsdr-tab5/ui/wwv_tick.cpp \
  -o "$build_dir/wwv_tick_tests"
"$build_dir/wwv_tick_tests" "$wwv_fixture"
printf '%s\n' "DECODER_REPLAY_CASE wwv_wav status=PASS input=$wwv_fixture format=WAV_PCM16"

printf '%s\n' 'DECODER_REPLAY_CASE acars_raw_iq status=MISSING reason=no_checked_in_raw_iq_fixture'
printf '%s\n' 'DECODER_REPLAY_CASE adsb_raw_iq status=MISSING reason=no_checked_in_raw_iq_fixture'
printf '%s\n' 'DECODER_REPLAY_CASE lora_raw_iq status=MISSING reason=corpus_manifest_points_to_ignored_local_artifacts'
printf '%s\n' 'DECODER_REPLAY_CASE ft8_real_wav status=MISSING reason=real_wav_is_downloaded_by_separate_ci_workflow'
printf '%s\n' 'DECODER_REPLAY_CASE sstv_real_wav status=MISSING reason=real_wav_is_optional_external_input'
printf '%s\n' 'DECODER_REPLAY_SUMMARY status=PASS validated=p25_iq,wwv_wav missing=acars_raw_iq,adsb_raw_iq,lora_raw_iq,ft8_real_wav,sstv_real_wav'
