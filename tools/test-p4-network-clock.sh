#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
ui=apps/orcsdr-tab5/ui
main=apps/orcsdr-p4-hosyond-console/main
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DORC_NETWORK_CLOCK_TEST -I"$ui" -I"$main" "$main/network_clock.cpp" "$ui/clock_sync_status.cpp" apps/orcsdr-p4-hosyond-console/tests/p4_network_clock_test.cpp -o "$out/clock"
"$out/clock"
c++ -std=c++17 -O1 -Wall -Wextra -Werror -I"$ui" tests/clock_sync_status_tests.cpp "$ui/clock_sync_status.cpp" -o "$out/policy"
"$out/policy"
