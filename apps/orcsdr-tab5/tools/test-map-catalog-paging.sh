#!/usr/bin/env bash
set -euo pipefail

# Invoke from the desired evidence directory. Always save the test log in
# that working directory, independent of this script's checkout location.
RUN_DIR="$PWD"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TAB5_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
LOG="$RUN_DIR/map-catalog-paging-test.log"

{
  echo "MAP_CATALOG_PAGING_TEST start"
  echo "source=$TAB5_DIR/tests/map_catalog_paging_tests.cpp"
  "${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$TAB5_DIR/tests/map_catalog_paging_tests.cpp" \
    -o "$TEMP_DIR/map_catalog_paging_tests"
  "$TEMP_DIR/map_catalog_paging_tests"
  echo "MAP_CATALOG_PAGING_TEST PASS"
} 2>&1 | tee "$LOG"
