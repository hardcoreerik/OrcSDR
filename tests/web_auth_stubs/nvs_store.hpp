#pragma once
#include <cstddef>
#include <cstdint>

namespace orcsdr {
class NvsStore {
 public:
  bool begin(const char* name, bool read_only = false);
  void end();
  bool is_key(const char* key) const;
  size_t bytes_length(const char* key) const;
  size_t get_bytes(const char* key, void* value, size_t size) const;
  bool get_bool(const char* key, bool fallback = false) const;
  bool put_bytes(const char* key, const void* value, size_t size);
  bool put_bool(const char* key, bool value);
 private:
  bool open_ = false;
  bool read_only_ = false;
};
}
