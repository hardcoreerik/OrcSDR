#pragma once
#include <cstddef>
#include <cstdint>
enum mbedtls_md_type_t { MBEDTLS_MD_SHA256 = 6 };
struct mbedtls_md_info_t {};
extern "C" const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t);
extern "C" int mbedtls_md_hmac(const mbedtls_md_info_t*, const unsigned char*, size_t, const unsigned char*, size_t, unsigned char*);
extern "C" int mbedtls_md(const mbedtls_md_info_t*, const unsigned char*, size_t, unsigned char*);
