// Web build: OrcSDR's NvsStore is flash-backed; here every instance shares one in-memory map, keyed by
// namespace + "." + key, that the page copies to and from localStorage (m5_nvs_export / m5_nvs_import in
// web_main.cpp). Same method names as the subset the dashboards use.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace orcsdr {

class NvsStore {
 public:
  uint32_t revision() const { return rev(); }
  const std::map<std::string, std::vector<uint8_t>>& entries() const { return shared(); }
  void set_raw(const std::string& key, std::vector<uint8_t> value) { shared()[key] = std::move(value); }

  bool begin(const char* name, bool = false) { ns_ = std::string(name) + "."; return true; }
  void end() {}
  bool is_key(const char* key) const { return shared().count(ns_ + key) != 0; }
  size_t bytes_length(const char* key) const {
    auto it = shared().find(ns_ + key);
    return it == shared().end() ? 0 : it->second.size();
  }
  size_t get_bytes(const char* key, void* value, size_t size) const {
    auto it = shared().find(ns_ + key);
    if (it == shared().end()) return 0;
    const size_t n = it->second.size() < size ? it->second.size() : size;
    memcpy(value, it->second.data(), n);
    return n;
  }
  std::string get_string(const char* key, const char* fallback = "") const {
    auto it = shared().find(ns_ + key);
    return it == shared().end() ? std::string(fallback) : std::string(it->second.begin(), it->second.end());
  }
  uint8_t get_u8(const char* key, uint8_t fallback = 0) const { return get<uint8_t>(key, fallback); }
  uint16_t get_u16(const char* key, uint16_t fallback = 0) const { return get<uint16_t>(key, fallback); }
  uint32_t get_u32(const char* key, uint32_t fallback = 0) const { return get<uint32_t>(key, fallback); }
  int32_t get_i32(const char* key, int32_t fallback = 0) const { return get<int32_t>(key, fallback); }
  bool get_bool(const char* key, bool fallback = false) const { return get<uint8_t>(key, fallback ? 1 : 0) != 0; }
  bool put_bytes(const char* key, const void* value, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(value);
    shared()[ns_ + key] = std::vector<uint8_t>(p, p + size);
    ++rev();
    return true;
  }
  bool put_string(const char* key, const char* value) { return put_bytes(key, value, strlen(value)); }
  bool put_u8(const char* key, uint8_t v) { return put_bytes(key, &v, sizeof(v)); }
  bool put_u16(const char* key, uint16_t v) { return put_bytes(key, &v, sizeof(v)); }
  bool put_u32(const char* key, uint32_t v) { return put_bytes(key, &v, sizeof(v)); }
  bool put_i32(const char* key, int32_t v) { return put_bytes(key, &v, sizeof(v)); }
  bool put_bool(const char* key, bool v) { return put_u8(key, v ? 1 : 0); }
  bool remove(const char* key) { ++rev(); return shared().erase(ns_ + key) != 0; }

  size_t getBytesLength(const char* key) const { return bytes_length(key); }
  size_t getBytes(const char* key, void* value, size_t size) const { return get_bytes(key, value, size); }
  uint8_t getUChar(const char* key, uint8_t f = 0) const { return get_u8(key, f); }
  uint16_t getUShort(const char* key, uint16_t f = 0) const { return get_u16(key, f); }
  uint32_t getUInt(const char* key, uint32_t f = 0) const { return get_u32(key, f); }
  int32_t getInt(const char* key, int32_t f = 0) const { return get_i32(key, f); }
  bool getBool(const char* key, bool f = false) const { return get_bool(key, f); }
  bool isKey(const char* key) const { return is_key(key); }
  bool putBytes(const char* key, const void* v, size_t n) { return put_bytes(key, v, n); }
  bool putUChar(const char* key, uint8_t v) { return put_u8(key, v); }
  bool putUShort(const char* key, uint16_t v) { return put_u16(key, v); }
  bool putUInt(const char* key, uint32_t v) { return put_u32(key, v); }
  bool putInt(const char* key, int32_t v) { return put_i32(key, v); }
  bool putBool(const char* key, bool v) { return put_bool(key, v); }

 private:
  template <typename T>
  T get(const char* key, T fallback) const {
    auto it = shared().find(ns_ + key);
    if (it == shared().end() || it->second.size() != sizeof(T)) return fallback;
    T v;
    memcpy(&v, it->second.data(), sizeof(T));
    return v;
  }
  static std::map<std::string, std::vector<uint8_t>>& shared() { static std::map<std::string, std::vector<uint8_t>> m; return m; }
  static uint32_t& rev() { static uint32_t r = 0; return r; }
  std::string ns_;
};

}  // namespace orcsdr
