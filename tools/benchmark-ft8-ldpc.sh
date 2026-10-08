#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

g++ -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Werror -pedantic \
  -Iapps/orcsdr-tab5/ui \
  tests/ft8_ldpc_decode_benchmark.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc.cpp \
  apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp \
  -o "$build_dir/ft8_ldpc_decode_benchmark"
"$build_dir/ft8_ldpc_decode_benchmark"
