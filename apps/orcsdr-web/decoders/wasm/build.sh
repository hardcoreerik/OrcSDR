#!/bin/sh
# Builds orcdec to WebAssembly (browser, worker and node) inside the emscripten image. /repo is the repository root.
set -e
R=/repo
D=$R/apps/orcsdr-web/decoders
OUT=$D/build/wasm
mkdir -p "$OUT"
. "$D/sources.sh"
emcc -O2 -std=c++17 -I"$D/include" $INCLUDES $SRCS \
  -sMODULARIZE=1 -sEXPORT_NAME=OrcDec -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,worker,node \
  -sEXPORTED_FUNCTIONS=_orcdec_info,_orcdec_create,_orcdec_destroy,_orcdec_reset,_orcdec_feed,_orcdec_command,_orcdec_poll,_orcdec_attachment,_orcdec_dropped,_orcdec_version,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,HEAP32,UTF8ToString,stringToUTF8,lengthBytesUTF8 \
  -o "$OUT/orcdec.js"
ls -l "$OUT"
