#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d /tmp/orcsdr-spectrum3d.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
args=(-std=c++17 -Wall -Wextra -Werror -Iapps/orcsdr-tab5/ui tests/spectrum_history_tests.cpp)
g++ "${args[@]}" -O2 -o "$test_dir/test"
"$test_dir/test" "${1:-$test_dir/terrain.ppm}"
g++ "${args[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -o "$test_dir/test-sanitized"
"$test_dir/test-sanitized"
