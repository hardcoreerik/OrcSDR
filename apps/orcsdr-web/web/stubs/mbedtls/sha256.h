// Web build: the lab's saved reports are hashed on the device. A browser keeps nothing on an SD card, so nothing reaches here.
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct { int unused; } mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
static inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
static inline int mbedtls_sha256_starts(mbedtls_sha256_context*, int) { return 0; }
static inline int mbedtls_sha256_update(mbedtls_sha256_context*, const unsigned char*, size_t) { return 0; }
static inline int mbedtls_sha256_finish(mbedtls_sha256_context*, unsigned char out[32]) { for (int i = 0; i < 32; ++i) out[i] = 0; return 0; }
