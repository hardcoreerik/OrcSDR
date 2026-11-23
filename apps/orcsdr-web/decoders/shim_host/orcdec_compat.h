// Host and WebAssembly builds: BSD string helpers that OrcSDR sources use and that C libraries do not all provide.
#pragma once
#include <string.h>
static inline size_t orcdec_strlcpy(char* dst, const char* src, size_t size) {
  const size_t n = strlen(src);
  if (size) {
    const size_t c = n >= size ? size - 1 : n;
    memcpy(dst, src, c);
    dst[c] = 0;
  }
  return n;
}
#define strlcpy orcdec_strlcpy
