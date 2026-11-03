#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/lora-rx-ui
orcmaps="${ORCSDR_ORCMAPS_ROOT:-apps/orcsdr-p4-hosyond-console/managed_components/orcmaps}"
g++ -std=c++17 -Wall -Wextra -Werror -Wno-unused-function -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I"$orcmaps/include" -Itests/lora_tx_ui_stubs -Itests/ft8_ui_stubs -Iapps/orcsdr-tab5/ui \
 tests/lora_rx_full_dashboard_host_tests.cpp apps/orcsdr-tab5/ui/lora_dashboard.cpp \
 "$orcmaps/src/core/geo.cpp" "$orcmaps/src/core/viewport.cpp" -o build/lora-rx-ui/integrated
build/lora-rx-ui/integrated
g++ -std=c++17 -Wall -Wextra -Werror \
 -Iapps/orcsdr-tab5/tests/maps/ui_stubs -Iapps/orcsdr-tab5/ui -I"$orcmaps/include" \
 apps/orcsdr-tab5/tests/maps/map_view_tests.cpp apps/orcsdr-tab5/ui/map_view.cpp \
 -o build/lora-rx-ui/map-view
build/lora-rx-ui/map-view
