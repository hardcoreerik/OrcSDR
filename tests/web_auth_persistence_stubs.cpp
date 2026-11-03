#include "nvs_store.hpp"
#include "nvs.h"
#include "nvs_test_support.hpp"
#include "crypto_test_support.hpp"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
struct RawValue { bool blob = true; std::vector<uint8_t> bytes; };
struct Handle { std::string ns; bool read_only = false; std::map<std::string, RawValue> staged; };
std::map<std::string, std::map<std::string, RawValue>> g_db;
std::map<nvs_handle_t, Handle> g_handles;
nvs_handle_t g_next_handle = 1;
std::string g_fail_key;
bool g_fail_commit = false;
size_t g_commits = 0;
int64_t g_time = 1;
uint8_t g_random = 1;
const mbedtls_md_info_t g_sha256{};

void fake_digest(const uint8_t* data, size_t length, uint8_t out[32]) {
  uint32_t state = 2166136261u;
  for (size_t i = 0; i < length; ++i) state = (state ^ data[i]) * 16777619u;
  for (size_t i = 0; i < 32; ++i) {
    state ^= static_cast<uint32_t>(i + 0x9e);
    state *= 16777619u;
    out[i] = static_cast<uint8_t>(state >> ((i & 3u) * 8));
  }
}
}

namespace web_auth_test_store {
void fail_put(const char* key) { g_fail_key = key ? key : ""; }
void fail_next_commit() { g_fail_commit = true; }
bool contains(const char* key) { return g_db["orcsdr_wauth"].count(key) != 0; }
size_t commit_count() { return g_commits; }
bool seed_blob(const char* key, const void* bytes, size_t size) {
  if (!key || (!bytes && size)) return false;
  RawValue v; v.blob = true; v.bytes.assign(static_cast<const uint8_t*>(bytes), static_cast<const uint8_t*>(bytes) + size);
  g_db["orcsdr_wauth"][key] = std::move(v); return true;
}
bool seed_u8(const char* key, uint8_t value) {
  if (!key) return false;
  RawValue v; v.blob = false; v.bytes.push_back(value); g_db["orcsdr_wauth"][key] = std::move(v); return true;
}
}

extern "C" esp_err_t nvs_open(const char* name, nvs_open_mode_t mode, nvs_handle_t* out) {
  if (!name || !out) return ESP_FAIL;
  const nvs_handle_t handle = g_next_handle++;
  g_handles.emplace(handle, Handle{name, mode == NVS_READONLY, {}});
  *out = handle; return ESP_OK;
}
extern "C" void nvs_close(nvs_handle_t handle) { g_handles.erase(handle); }
extern "C" esp_err_t nvs_commit(nvs_handle_t handle) {
  auto it = g_handles.find(handle);
  if (it == g_handles.end() || it->second.read_only) return ESP_FAIL;
  ++g_commits;
  if (g_fail_commit) { g_fail_commit = false; return ESP_FAIL; }
  auto& db = g_db[it->second.ns];
  for (auto& item : it->second.staged) db[item.first] = item.second;
  it->second.staged.clear(); return ESP_OK;
}
extern "C" esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* out, size_t* size) {
  auto h = g_handles.find(handle);
  if (h == g_handles.end() || !key || !size) return ESP_FAIL;
  auto ns = g_db.find(h->second.ns);
  if (ns == g_db.end()) return ESP_ERR_NVS_NOT_FOUND;
  auto value = ns->second.find(key);
  if (value == ns->second.end() || !value->second.blob) return ESP_ERR_NVS_NOT_FOUND;
  if (!out) { *size = value->second.bytes.size(); return ESP_OK; }
  if (*size < value->second.bytes.size()) return ESP_FAIL;
  std::memcpy(out, value->second.bytes.data(), value->second.bytes.size()); *size = value->second.bytes.size(); return ESP_OK;
}
extern "C" esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out) {
  if (!out) return ESP_FAIL;
  size_t size = 1; auto h = g_handles.find(handle);
  if (h == g_handles.end()) return ESP_FAIL;
  auto ns = g_db.find(h->second.ns); if (ns == g_db.end()) return ESP_ERR_NVS_NOT_FOUND;
  auto value = ns->second.find(key);
  if (value == ns->second.end() || value->second.blob || value->second.bytes.size() != 1) return ESP_ERR_NVS_NOT_FOUND;
  *out = value->second.bytes[0]; (void)size; return ESP_OK;
}
extern "C" esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* bytes, size_t size) {
  auto h = g_handles.find(handle);
  if (h == g_handles.end() || h->second.read_only || !key || (!bytes && size)) return ESP_FAIL;
  if (g_fail_key == key) { g_fail_key.clear(); return ESP_FAIL; }
  RawValue value; value.blob = true;
  if (size) value.bytes.assign(static_cast<const uint8_t*>(bytes), static_cast<const uint8_t*>(bytes) + size);
  h->second.staged[key] = std::move(value); return ESP_OK;
}
extern "C" esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) {
  auto h = g_handles.find(handle);
  if (h == g_handles.end() || h->second.read_only || !key) return ESP_FAIL;
  if (g_fail_key == key) { g_fail_key.clear(); return ESP_FAIL; }
  RawValue raw; raw.blob = false; raw.bytes.push_back(value); h->second.staged[key] = std::move(raw); return ESP_OK;
}
extern "C" esp_err_t nvs_entry_find(const char*, const char* ns, nvs_type_t, nvs_iterator_t* it) {
  if (!it || !ns) return ESP_FAIL;
  *it = nullptr; return ESP_ERR_NVS_NOT_FOUND;
}
extern "C" esp_err_t nvs_entry_info(nvs_iterator_t, nvs_entry_info_t*) { return ESP_FAIL; }
extern "C" esp_err_t nvs_entry_next(nvs_iterator_t*) { return ESP_ERR_NVS_NOT_FOUND; }

namespace orcsdr {
bool NvsStore::begin(const char* name, bool read_only) {
  end(); nvs_handle_t handle{};
  if (nvs_open(name, read_only ? NVS_READONLY : NVS_READWRITE, &handle) != ESP_OK) return false;
  // The facade only needs its access mode here; each operation opens a temporary handle via static fake functions.
  nvs_close(handle); read_only_ = read_only; open_ = true; return true;
}
void NvsStore::end() { open_ = false; }
bool NvsStore::is_key(const char* key) const {
  return open_ && key && g_db["orcsdr_wauth"].count(key) != 0;
}
size_t NvsStore::bytes_length(const char* key) const {
  if (!open_ || !key) return 0;
  nvs_handle_t h{}; if (nvs_open("orcsdr_wauth", NVS_READONLY, &h) != ESP_OK) return 0;
  size_t n=0; const auto e=nvs_get_blob(h,key,nullptr,&n); nvs_close(h); return e==ESP_OK?n:0;
}
size_t NvsStore::get_bytes(const char* key, void* out, size_t size) const {
  if (!open_ || !key || !out) return 0;
  nvs_handle_t h{}; if (nvs_open("orcsdr_wauth", NVS_READONLY, &h) != ESP_OK) return 0;
  size_t n=size; const auto e=nvs_get_blob(h,key,out,&n); nvs_close(h); return e==ESP_OK?n:0;
}
bool NvsStore::get_bool(const char* key, bool fallback) const {
  if (!open_ || !key) return fallback;
  nvs_handle_t h{}; if (nvs_open("orcsdr_wauth", NVS_READONLY, &h) != ESP_OK) return fallback;
  uint8_t v=fallback?1:0; const auto e=nvs_get_u8(h,key,&v); nvs_close(h); return e==ESP_OK?v!=0:fallback;
}
bool NvsStore::put_bytes(const char* key, const void* data, size_t size) {
  if (!open_ || read_only_) return false;
  nvs_handle_t h{}; if (nvs_open("orcsdr_wauth", NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok=nvs_set_blob(h,key,data,size)==ESP_OK && nvs_commit(h)==ESP_OK; nvs_close(h); return ok;
}
bool NvsStore::put_bool(const char* key, bool value) {
  if (!open_ || read_only_) return false;
  nvs_handle_t h{}; if (nvs_open("orcsdr_wauth", NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok=nvs_set_u8(h,key,value?1:0)==ESP_OK && nvs_commit(h)==ESP_OK; nvs_close(h); return ok;
}
}

void test_pbkdf2(const unsigned char* password,size_t password_len,const unsigned char* salt,size_t salt_len,unsigned char out[32]) {
  std::vector<uint8_t> data(password,password+password_len); data.insert(data.end(),salt,salt+salt_len); fake_digest(data.data(),data.size(),out);
}
void test_hmac(const unsigned char* key,size_t key_len,const unsigned char* data,size_t data_len,unsigned char out[32]) {
  std::vector<uint8_t> input(key,key+key_len); input.insert(input.end(),data,data+data_len); fake_digest(input.data(),input.size(),out);
}
extern "C" const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t) { return &g_sha256; }
extern "C" int mbedtls_md_hmac(const mbedtls_md_info_t*,const unsigned char* key,size_t key_len,const unsigned char* data,size_t len,unsigned char* out) {
  test_hmac(key,key_len,data,len,out); return 0;
}
extern "C" int mbedtls_md(const mbedtls_md_info_t*,const unsigned char* data,size_t len,unsigned char* out) { fake_digest(data,len,out); return 0; }
extern "C" int mbedtls_pkcs5_pbkdf2_hmac_ext(mbedtls_md_type_t,const unsigned char* password,size_t password_len,const unsigned char* salt,size_t salt_len,unsigned,uint32_t,unsigned char* out) {
  test_pbkdf2(password,password_len,salt,salt_len,out); return 0;
}
extern "C" void esp_fill_random(void* buffer,size_t length) { auto* p=static_cast<uint8_t*>(buffer); while(length--)*p++=g_random++; }
extern "C" int64_t esp_timer_get_time() { return g_time+=100; }
extern "C" SemaphoreHandle_t xSemaphoreCreateMutex() { static int mutex; return &mutex; }
extern "C" int xSemaphoreTake(SemaphoreHandle_t,unsigned) { return 1; }
extern "C" int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
