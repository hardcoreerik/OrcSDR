#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace orcsdr::test_sha {
inline bool fail_start = false;
inline bool fail_update = false;
inline bool fail_finish = false;
}  // namespace orcsdr::test_sha

struct mbedtls_sha256_context {
  uint64_t state[4]{};
  size_t total = 0;
  bool active = false;
};

inline void mbedtls_sha256_init(mbedtls_sha256_context* ctx) {
  if (ctx) *ctx = {};
}

inline void mbedtls_sha256_free(mbedtls_sha256_context* ctx) {
  if (ctx) *ctx = {};
}

inline int mbedtls_sha256_starts(mbedtls_sha256_context* ctx, int) {
  if (!ctx || orcsdr::test_sha::fail_start) return -1;
  ctx->state[0] = 0x6a09e667bb67ae85ULL;
  ctx->state[1] = 0x3c6ef372a54ff53aULL;
  ctx->state[2] = 0x510e527f9b05688cULL;
  ctx->state[3] = 0x1f83d9ab5be0cd19ULL;
  ctx->total = 0;
  ctx->active = true;
  return 0;
}

inline int mbedtls_sha256_update(mbedtls_sha256_context* ctx, const unsigned char* input, size_t ilen) {
  if (!ctx || !ctx->active || orcsdr::test_sha::fail_update) return -1;
  for (size_t i = 0; i < ilen; ++i) {
    const size_t lane = (ctx->total + i) & 3u;
    ctx->state[lane] ^= static_cast<uint64_t>(input[i]) + 0x9e3779b97f4a7c15ULL + (ctx->state[lane] << 6) +
                        (ctx->state[lane] >> 2);
  }
  ctx->total += ilen;
  return 0;
}

inline int mbedtls_sha256_finish(mbedtls_sha256_context* ctx, unsigned char output[32]) {
  if (!ctx || !ctx->active || orcsdr::test_sha::fail_finish) return -1;
  uint64_t out[4];
  for (size_t i = 0; i < 4; ++i) {
    out[i] = ctx->state[i] ^ (static_cast<uint64_t>(ctx->total) * (0xbf58476d1ce4e5b9ULL + i));
  }
  std::memcpy(output, out, 32);
  return 0;
}
