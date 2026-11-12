#!/bin/sh
# Builds the host replay tool inside the emscripten image (it carries g++ 11). /repo is the repository root.
set -e
R=/repo
D=$R/apps/orcsdr-web/decoders
OUT=$D/build
mkdir -p "$OUT"
. "$D/sources.sh"
g++ -O2 -std=c++17 -Wall -I"$D/include" $INCLUDES $SRCS "$D/host/orcdec_replay.cpp" -o "$OUT/orcdec_replay"
ls -l "$OUT/orcdec_replay"
