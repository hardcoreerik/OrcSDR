#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -O1 -Wall -Wextra -Werror \
  -I"$root/components/orcsdr_gnss/include" \
  "$root/components/orcsdr_gnss/orcsdr_gnss_parser.c" \
  "$root/tests/p4_gnss_parser_test.c" \
  -o "$build/p4_gnss_parser_test"
"$build/p4_gnss_parser_test"
