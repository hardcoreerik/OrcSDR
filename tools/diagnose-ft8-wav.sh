#!/usr/bin/env bash
# Host-only miss classification for a real FT8/FT4 recording against a WSJT-X reference list.
#   bash tools/diagnose-ft8-wav.sh <wav> <ft8|ft4> <rows_per_symbol> <bins_per_tone> [--ref FILE] [--cap N] [--list] ...
set -euo pipefail
if [[ $# -lt 4 ]]; then
  echo "usage: $0 <12k-mono-i16.wav> <ft8|ft4> <rows_per_symbol> <bins_per_tone> [--ref FILE] [--cap N] [--min-score X] [--all N] [--list]" >&2
  exit 2
fi
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
g++ -std=c++17 -O3 -Wall -Wextra -Werror -pedantic -Iapps/orcsdr-tab5/ui \
  tools/ft8-wav-diagnose.cpp \
  apps/orcsdr-tab5/ui/ft8_mode.cpp apps/orcsdr-tab5/ui/ft8_spectral.cpp apps/orcsdr-tab5/ui/ft8_sync.cpp \
  apps/orcsdr-tab5/ui/ft8_demod.cpp apps/orcsdr-tab5/ui/ft8_pipeline.cpp apps/orcsdr-tab5/ui/ft8_snr.cpp apps/orcsdr-tab5/ui/ft8_message.cpp \
  apps/orcsdr-tab5/ui/ft8_codec.cpp apps/orcsdr-tab5/ui/ft8_ldpc.cpp apps/orcsdr-tab5/ui/ft8_ldpc_decode.cpp \
  -o "$build_dir/ft8-wav-diagnose"
"$build_dir/ft8-wav-diagnose" "$@"
