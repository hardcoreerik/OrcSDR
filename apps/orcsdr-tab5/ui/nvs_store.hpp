#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <nvs.h>

namespace orcsdr {

class NvsStore {
 public:
  ~NvsStore();
  bool begin(const char* name, bool read_only = false);
  void end();
  bool is_key(const char* key) const;
  size_t bytes_length(const char* key) const;
  size_t get_bytes(const char* key, void* value, size_t size) const;
  std::string get_string(const char* key, const char* fallback = "") const;
  uint8_t get_u8(const char* key, uint8_t fallback = 0) const;
  uint16_t get_u16(const char* key, uint16_t fallback = 0) const;
  uint32_t get_u32(const char* key, uint32_t fallback = 0) const;
  int32_t get_i32(const char* key, int32_t fallback = 0) const;
  bool get_bool(const char* key, bool fallback = false) const;
  bool put_bytes(const char* key, const void* value, size_t size);
  bool put_string(const char* key, const char* value);
  bool put_u8(const char* key, uint8_t value);
  bool put_u16(const char* key, uint16_t value);
  bool put_u32(const char* key, uint32_t value);
  bool put_i32(const char* key, int32_t value);
  bool put_bool(const char* key, bool value);
  bool remove(const char* key);

  // Transitional spellings keep the existing settings logic readable while
  // all storage is served directly by ESP-IDF NVS.
  size_t getBytesLength(const char* key) const { return bytes_length(key); }
  size_t getBytes(const char* key, void* value, size_t size) const { return get_bytes(key, value, size); }
  std::string getString(const char* key, const char* fallback = "") const { return get_string(key, fallback); }
  uint8_t getUChar(const char* key, uint8_t fallback = 0) const { return get_u8(key, fallback); }
  uint16_t getUShort(const char* key, uint16_t fallback = 0) const { return get_u16(key, fallback); }
  uint32_t getUInt(const char* key, uint32_t fallback = 0) const { return get_u32(key, fallback); }
  int32_t getInt(const char* key, int32_t fallback = 0) const { return get_i32(key, fallback); }
  bool getBool(const char* key, bool fallback = false) const { return get_bool(key, fallback); }
  bool isKey(const char* key) const { return is_key(key); }
  bool putBytes(const char* key, const void* value, size_t size) { return put_bytes(key, value, size); }
  bool putString(const char* key, const char* value) { return put_string(key, value); }
  bool putUChar(const char* key, uint8_t value) { return put_u8(key, value); }
  bool putUShort(const char* key, uint16_t value) { return put_u16(key, value); }
  bool putUInt(const char* key, uint32_t value) { return put_u32(key, value); }
  bool putInt(const char* key, int32_t value) { return put_i32(key, value); }
  bool putBool(const char* key, bool value) { return put_bool(key, value); }

 private:
  bool commit(esp_err_t result);
  nvs_handle_t handle_ = 0;
  bool open_ = false;
  char namespace_[16]{};
};

}  // namespace orcsdr
