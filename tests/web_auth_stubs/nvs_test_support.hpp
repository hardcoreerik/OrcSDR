#pragma once
#include <cstddef>
#include <cstdint>
namespace web_auth_test_store {
void fail_next_write_before();
void fail_next_write_after();
bool contains(const char* key);
size_t write_count();
size_t mutex_create_count();
bool seed_blob(const char* key, const void* bytes, size_t size);
bool seed_u8(const char* key, uint8_t value);
}
