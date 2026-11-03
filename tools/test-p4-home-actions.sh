#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/p4-home-actions
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror \
  -Iapps/orcsdr-p4-hosyond-console/main -Iapps/orcsdr-tab5/ui \
  tests/p4_home_web_actions_test.cpp apps/orcsdr-tab5/ui/filter_standards.cpp \
  -o build/p4-home-actions/test
build/p4-home-actions/test | tee build/p4-home-actions/result.log
