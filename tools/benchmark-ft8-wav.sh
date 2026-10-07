#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
  echo "usage: $0 <12k-mono-i16.wav> [rows_per_symbol=2] [bins_per_tone=1]" >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

g++ -std=c++17 -O3 -Wall -Wextra -Werror -pedantic   -Iapps/orcsdr-tab5/ui   tools/ft8-wav-benchmark.cpp   apps/orcsdr-tab5/ui/ft8_mode.cpp   apps/orcsdr-tab5/ui/ft8_spectral.cpp   apps/orcsdr-tab5/ui/ft8_sync.cpp   apps/orcsdr-tab5/ui/ft8_demod.cpp   apps/orcsdr-tab5/ui/ft8_pipeline.cpp   apps/orcsdr-tab5/ui/ft8_message.cpp   apps/orcsdr-tab5/ui/ft8_codec.cpp   apps/orcsdr-tab5/ui/ft8_ldpc.cpp   apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp   -o "$build_dir/ft8-wav-benchmark"

"$build_dir/ft8-wav-benchmark" "$@"
