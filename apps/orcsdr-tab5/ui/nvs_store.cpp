#include "nvs_store.hpp"

#include <cstring>
#include <vector>

namespace orcsdr {

NvsStore::~NvsStore() { end(); }

bool NvsStore::begin(const char* name, bool read_only) {
  end();
  if (name == nullptr) return false;
  if (nvs_open(name, read_only ? NVS_READONLY : NVS_READWRITE, &handle_) != ESP_OK) return false;
  strlcpy(namespace_, name, sizeof(namespace_));
  open_ = true;
  return true;
}

void NvsStore::end() {
  if (open_) nvs_close(handle_);
  handle_ = 0;
  open_ = false;
  namespace_[0] = '\0';
}

bool NvsStore::is_key(const char* key) const {
  if (!open_ || key == nullptr) return false;
  nvs_iterator_t iterator = nullptr;
  if (nvs_entry_find(NVS_DEFAULT_PART_NAME, namespace_, NVS_TYPE_ANY, &iterator) != ESP_OK) return false;
  while (iterator != nullptr) {
    nvs_entry_info_t entry{};
    nvs_entry_info(iterator, &entry);
    const bool found = strcmp(entry.key, key) == 0;
    nvs_entry_next(&iterator);
    if (found) return true;
  }
  return false;
}

size_t NvsStore::bytes_length(const char* key) const {
  size_t size = 0;
  return open_ && key != nullptr && nvs_get_blob(handle_, key, nullptr, &size) == ESP_OK ? size : 0;
}

size_t NvsStore::get_bytes(const char* key, void* value, size_t size) const {
  if (!open_ || key == nullptr || value == nullptr) return 0;
  size_t actual = size;
  return nvs_get_blob(handle_, key, value, &actual) == ESP_OK ? actual : 0;
}

std::string NvsStore::get_string(const char* key, const char* fallback) const {
  if (!open_ || key == nullptr) return fallback ? fallback : "";
  size_t size = 0;
  if (nvs_get_str(handle_, key, nullptr, &size) != ESP_OK || size == 0) return fallback ? fallback : "";
  std::vector<char> value(size);
  return nvs_get_str(handle_, key, value.data(), &size) == ESP_OK ? value.data() : (fallback ? fallback : "");
}

uint8_t NvsStore::get_u8(const char* key, uint8_t fallback) const { uint8_t value = fallback; return open_ && nvs_get_u8(handle_, key, &value) == ESP_OK ? value : fallback; }
uint16_t NvsStore::get_u16(const char* key, uint16_t fallback) const { uint16_t value = fallback; return open_ && nvs_get_u16(handle_, key, &value) == ESP_OK ? value : fallback; }
uint32_t NvsStore::get_u32(const char* key, uint32_t fallback) const { uint32_t value = fallback; return open_ && nvs_get_u32(handle_, key, &value) == ESP_OK ? value : fallback; }
int32_t NvsStore::get_i32(const char* key, int32_t fallback) const { int32_t value = fallback; return open_ && nvs_get_i32(handle_, key, &value) == ESP_OK ? value : fallback; }
bool NvsStore::get_bool(const char* key, bool fallback) const { return get_u8(key, fallback ? 1 : 0) != 0; }

bool NvsStore::commit(esp_err_t result) { return open_ && result == ESP_OK && nvs_commit(handle_) == ESP_OK; }
bool NvsStore::put_bytes(const char* key, const void* value, size_t size) { return open_ && key && value && commit(nvs_set_blob(handle_, key, value, size)); }
bool NvsStore::put_string(const char* key, const char* value) { return open_ && key && value && commit(nvs_set_str(handle_, key, value)); }
bool NvsStore::put_u8(const char* key, uint8_t value) { return open_ && key && commit(nvs_set_u8(handle_, key, value)); }
bool NvsStore::put_u16(const char* key, uint16_t value) { return open_ && key && commit(nvs_set_u16(handle_, key, value)); }
bool NvsStore::put_u32(const char* key, uint32_t value) { return open_ && key && commit(nvs_set_u32(handle_, key, value)); }
bool NvsStore::put_i32(const char* key, int32_t value) { return open_ && key && commit(nvs_set_i32(handle_, key, value)); }
bool NvsStore::put_bool(const char* key, bool value) { return put_u8(key, value ? 1 : 0); }
bool NvsStore::remove(const char* key) { return open_ && key && commit(nvs_erase_key(handle_, key)); }

}  // namespace orcsdr
