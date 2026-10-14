#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
bin="$(mktemp)"
trap 'rm -f "$bin"' EXIT
{
  date -u +'%Y-%m-%dT%H:%M:%SZ'
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic -I "$here/../include" "$here/test_layout.cpp" -o "$bin"
  "$bin"
} 2>&1 | tee "$here/host-test.log"
