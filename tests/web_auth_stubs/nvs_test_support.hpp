#pragma once
#include <cstddef>
#include <cstdint>
namespace web_auth_test_store {
void fail_put(const char* key);
void fail_next_commit();
bool contains(const char* key);
size_t commit_count();
bool seed_blob(const char* key, const void* bytes, size_t size);
bool seed_u8(const char* key, uint8_t value);
}
