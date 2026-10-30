#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
# VDL2 proof-of-concept tests; runs against the ACTUAL OrcSDR acars_core.hpp.
# These tests do not constitute off-air/Tab5 decode validation.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cxx="${CXX:-c++}"
args=(-std=c++17 -O1 -g -Wall -Wextra -Werror -pedantic -fno-omit-frame-pointer)
if [[ "${ORC_VDL2_SANITIZE:-1}" == "1" ]]; then
  args+=(-fsanitize=address,undefined)
fi
includes=(-I "$root/apps/orcsdr-tab5/ui")
"$cxx" "${args[@]}" "${includes[@]}" "$root/apps/orcsdr-tab5/ui/vdl2_frames.cpp" "$root/tests/vdl2_frames_test.cpp" -o "$build/vdl2_frames_test"
"$build/vdl2_frames_test"
"$cxx" "${args[@]}" "${includes[@]}" "$root/apps/orcsdr-tab5/ui/vdl2_burst.cpp" "$root/tests/vdl2_burst_test.cpp" -o "$build/vdl2_burst_test"
"$build/vdl2_burst_test"
printf 'VDL2 host-only synthetic tests: PASS (not an off-air decode)\n'
