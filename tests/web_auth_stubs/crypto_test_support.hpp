#pragma once
#include <cstddef>
#include <cstdint>
void test_pbkdf2(const unsigned char* password, size_t password_len, const unsigned char* salt, size_t salt_len, unsigned char out[32]);
void test_hmac(const unsigned char* key, size_t key_len, const unsigned char* data, size_t data_len, unsigned char out[32]);
