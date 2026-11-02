#!/usr/bin/env bash
# Focused host-only tests. No ESP-IDF, device flashing or RF transmission.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/lora-tx-ui
cxx="${CXX:-g++}"
flags=(-std=c++17 -Wall -Wextra -Werror -O1 -g)
"$cxx" "${flags[@]}" -Itests/acars_ui_stubs -Iapps/orcsdr-tab5/ui \
  tests/lora_tx_views_render_tests.cpp -o build/lora-tx-ui/layout
build/lora-tx-ui/layout 2>&1 | tee build/lora-tx-ui/layout.log
"$cxx" "${flags[@]}" -Iapps/orcsdr-tab5/ui \
  tests/lora_tx_policy_tests.cpp -o build/lora-tx-ui/policy
build/lora-tx-ui/policy 2>&1 | tee build/lora-tx-ui/policy.log
echo "lora TX UI host suite PASS" | tee build/lora-tx-ui/result.log
