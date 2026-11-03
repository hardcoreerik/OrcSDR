#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -O1 -Wall -Wextra -Werror \
  -I"$root/tests/p4_gnss_service_fakes" \
  -I"$root/components/orcsdr_gnss/include" \
  -I"$root/apps/orcsdr-p4-hosyond-console/main" \
  "$root/apps/orcsdr-p4-hosyond-console/main/gnss_service.cpp" \
  "$root/components/orcsdr_gnss/orcsdr_gnss_parser.c" \
  "$root/tests/p4_gnss_service_test.cpp" -o "$build/p4_gnss_service_test"
"$build/p4_gnss_service_test"
