#pragma once
#include <cstddef>
#include <cstdint>
#include "md.h"
extern "C" int mbedtls_pkcs5_pbkdf2_hmac_ext(mbedtls_md_type_t, const unsigned char*, size_t,
                                                const unsigned char*, size_t, unsigned, uint32_t, unsigned char*);
