// The slice of mbedTLS's AES that OrcSDR's Meshtastic decryption uses (encrypt-only key schedule, CTR mode), so the decoder builds the same
// way on the host, in WebAssembly and in the node's firmware, whatever mbedTLS version the SDK carries. Plain FIPS-197 AES-128/256.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint8_t round_keys[15 * 16];
  int rounds;
} mbedtls_aes_context;

namespace lora_aes_detail {
struct Tables {
  uint8_t sbox[256];
  Tables() {
    // the multiplicative inverse in GF(2^8), then the affine transform
    uint8_t p = 1, q = 1;
    do {
      p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
      q ^= (uint8_t)(q << 1);
      q ^= (uint8_t)(q << 2);
      q ^= (uint8_t)(q << 4);
      if (q & 0x80) q ^= 0x09;
      const uint8_t x = (uint8_t)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
      sbox[p] = (uint8_t)(x ^ 0x63);
    } while (p != 1);
    sbox[0] = 0x63;
  }
};
inline const Tables& tables() {
  static const Tables t;
  return t;
}
inline uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
}  // namespace lora_aes_detail

static inline void mbedtls_aes_init(mbedtls_aes_context* ctx) { memset(ctx, 0, sizeof *ctx); }
static inline void mbedtls_aes_free(mbedtls_aes_context* ctx) {
  if (ctx) memset(ctx, 0, sizeof *ctx);
}

static inline int mbedtls_aes_setkey_enc(mbedtls_aes_context* ctx, const unsigned char* key, unsigned keybits) {
  if (keybits != 128 && keybits != 256) return -1;
  const auto& t = lora_aes_detail::tables();
  const int nk = (int)(keybits / 32);
  ctx->rounds = nk + 6;
  const int words = 4 * (ctx->rounds + 1);
  uint8_t* w = ctx->round_keys;
  memcpy(w, key, (size_t)nk * 4);
  uint8_t rcon = 1;
  for (int i = nk; i < words; ++i) {
    uint8_t temp[4];
    memcpy(temp, w + 4 * (i - 1), 4);
    if (i % nk == 0) {
      const uint8_t t0 = temp[0];
      temp[0] = (uint8_t)(t.sbox[temp[1]] ^ rcon);
      temp[1] = t.sbox[temp[2]];
      temp[2] = t.sbox[temp[3]];
      temp[3] = t.sbox[t0];
      rcon = lora_aes_detail::xtime(rcon);
    } else if (nk > 6 && i % nk == 4) {
      for (int k = 0; k < 4; ++k) temp[k] = t.sbox[temp[k]];
    }
    for (int k = 0; k < 4; ++k) w[4 * i + k] = (uint8_t)(w[4 * (i - nk) + k] ^ temp[k]);
  }
  return 0;
}

static inline void lora_aes_encrypt_block(const mbedtls_aes_context* ctx, const uint8_t in[16], uint8_t out[16]) {
  const auto& t = lora_aes_detail::tables();
  uint8_t s[16];
  for (int i = 0; i < 16; ++i) s[i] = (uint8_t)(in[i] ^ ctx->round_keys[i]);
  for (int round = 1; round <= ctx->rounds; ++round) {
    uint8_t b[16];
    for (int i = 0; i < 16; ++i) b[i] = t.sbox[s[i]];
    // ShiftRows (the state is column-major: byte index = 4 * column + row)
    uint8_t r[16];
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) r[4 * c + row] = b[4 * ((c + row) % 4) + row];
    if (round < ctx->rounds) {
      for (int c = 0; c < 4; ++c) {
        const uint8_t a0 = r[4 * c], a1 = r[4 * c + 1], a2 = r[4 * c + 2], a3 = r[4 * c + 3];
        const uint8_t all = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
        s[4 * c] = (uint8_t)(a0 ^ all ^ lora_aes_detail::xtime((uint8_t)(a0 ^ a1)));
        s[4 * c + 1] = (uint8_t)(a1 ^ all ^ lora_aes_detail::xtime((uint8_t)(a1 ^ a2)));
        s[4 * c + 2] = (uint8_t)(a2 ^ all ^ lora_aes_detail::xtime((uint8_t)(a2 ^ a3)));
        s[4 * c + 3] = (uint8_t)(a3 ^ all ^ lora_aes_detail::xtime((uint8_t)(a3 ^ a0)));
      }
    } else {
      memcpy(s, r, 16);
    }
    for (int i = 0; i < 16; ++i) s[i] ^= ctx->round_keys[16 * round + i];
  }
  memcpy(out, s, 16);
}

static inline int mbedtls_aes_crypt_ctr(mbedtls_aes_context* ctx, size_t length, size_t* nc_off, unsigned char nonce_counter[16],
                                        unsigned char stream_block[16], const unsigned char* input, unsigned char* output) {
  size_t n = *nc_off;
  for (size_t i = 0; i < length; ++i) {
    if (n == 0) {
      lora_aes_encrypt_block(ctx, nonce_counter, stream_block);
      for (int k = 15; k >= 0; --k)
        if (++nonce_counter[k] != 0) break;
    }
    output[i] = (unsigned char)(input[i] ^ stream_block[n]);
    n = (n + 1) & 0x0f;
  }
  *nc_off = n;
  return 0;
}
