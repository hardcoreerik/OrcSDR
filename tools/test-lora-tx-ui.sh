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
"$cxx" "${flags[@]}" -Itests/acars_ui_stubs -Iapps/orcsdr-tab5/ui \
  tests/lora_tx_simulation_tests.cpp -o build/lora-tx-ui/simulation
build/lora-tx-ui/simulation 2>&1 | tee build/lora-tx-ui/simulation.log
"$cxx" "${flags[@]}" -Iapps/orcsdr-tab5/ui \
  tests/lora_tx_policy_tests.cpp -o build/lora-tx-ui/policy
build/lora-tx-ui/policy 2>&1 | tee build/lora-tx-ui/policy.log
# Compile the actual integrated dashboard source, not only the isolated view header.
# ESP-IDF and physical framebuffer behavior still require a native device build.
"$cxx" "${flags[@]}" -fsyntax-only \
  -Itests/lora_tx_ui_stubs -Itests/ft8_ui_stubs -Itests/acars_ui_stubs \
  -Iapps/orcsdr-tab5/ui apps/orcsdr-tab5/ui/lora_dashboard.cpp \
  2>&1 | tee build/lora-tx-ui/integrated-syntax.log
# Link and execute the real lora_dashboard.cpp with neutral host dependencies.
# Exercises Tab5 RX-only, operator buttons, input navigation and hot-unplug.
# The production source has a pre-existing unused draw_metric() helper.
# Only the integrated host linker gate suppresses that unrelated warning.
"$cxx" "${flags[@]}" -Wno-unused-function \
  -Itests/lora_tx_ui_stubs -Itests/ft8_ui_stubs \
  -Iapps/orcsdr-tab5/ui \
  tests/lora_tx_full_dashboard_host_tests.cpp \
  apps/orcsdr-tab5/ui/lora_dashboard.cpp \
  -o build/lora-tx-ui/integrated
build/lora-tx-ui/integrated 2>&1 | tee build/lora-tx-ui/integrated.log
echo "lora TX UI host suite PASS" | tee build/lora-tx-ui/result.log
