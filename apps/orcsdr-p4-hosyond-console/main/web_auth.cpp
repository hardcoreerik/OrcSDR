#include "web_auth.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"

namespace web_auth {
namespace {
constexpr char TAG[] = "WEBAUTH";
constexpr char kFactoryPassword[] = "password";
constexpr int64_t kNonceLifeUs = 30LL * 1000 * 1000;
constexpr int64_t kIdleUs = 5LL * 60 * 1000 * 1000;
constexpr int64_t kHardUs = 60LL * 60 * 1000 * 1000;
constexpr int kNonces = 4, kPeers = 8;
constexpr size_t kCredentialBytes = 60;
constexpr uint8_t kCredentialVersion = 1;

struct Nonce {
  uint8_t value[16];
  int64_t expires_us = 0;  // 0 = free
};
struct Session {
  uint8_t id[8];
  uint8_t key[32];
  uint64_t last_seq = 0;
  int64_t created_us = 0, last_us = 0;
  bool used = false;
};
struct Peer {
  uint32_t ip = 0;
  uint8_t fails = 0;
  int64_t until_us = 0;
  int64_t last_us = 0;
};

SemaphoreHandle_t g_mutex = nullptr;
uint8_t g_key[32], g_salt[16];
bool g_default = true;
std::atomic<bool> g_ready{false};
Nonce g_nonces[kNonces];
Session g_sessions[kMaxSessions];
Peer g_peers[kPeers];

struct Lock {
  Lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_mutex); }
};

int64_t now_us() { return esp_timer_get_time(); }

bool hmac(const uint8_t* key, size_t key_len, const uint8_t* data, size_t len, uint8_t out[32]) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return info && mbedtls_md_hmac(info, key, key_len, data, len, out) == 0;
}

// HMAC(key, label || nonce [|| suffix])
bool labelled(const uint8_t* key, const char* label, const uint8_t nonce[16], const char* suffix, uint8_t out[32]) {
  uint8_t buf[48];
  size_t n = std::strlen(label);
  const size_t s = suffix ? std::strlen(suffix) : 0;
  if (n + 16 + s > sizeof(buf)) return false;
  std::memcpy(buf, label, n);
  std::memcpy(buf + n, nonce, 16);
  if (s) std::memcpy(buf + n + 16, suffix, s);
  return hmac(key, 32, buf, n + 16 + s, out);
}

bool equal(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; ++i) d |= a[i] ^ b[i];
  return d == 0;
}

void to_hex(const uint8_t* in, size_t n, char* out) {
  static const char* digits = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = digits[in[i] >> 4];
    out[2 * i + 1] = digits[in[i] & 15];
  }
  out[2 * n] = '\0';
}

int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Exactly `n` bytes of hex, nothing before or after.
bool from_hex(const char* in, uint8_t* out, size_t n) {
  if (!in || std::strlen(in) != 2 * n) return false;
  for (size_t i = 0; i < n; ++i) {
    const int hi = nibble(in[2 * i]), lo = nibble(in[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

bool derive(const char* password, const uint8_t salt[16], uint8_t key[32]) {
  return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(password), std::strlen(password), salt, 16, kIterations, 32, key) == 0;
}

uint32_t crc32(const uint8_t* bytes, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void write_u32_le(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t read_u32_le(const uint8_t* in) {
  return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
         (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
}

void encode_credentials(const uint8_t salt[16], const uint8_t key[32], bool is_default, uint8_t out[kCredentialBytes]) {
  std::memset(out, 0, kCredentialBytes);
  out[0] = 'W'; out[1] = 'A'; out[2] = 'U'; out[3] = 'T';
  out[4] = kCredentialVersion;
  out[5] = is_default ? 1 : 0;
  std::memcpy(out + 8, salt, 16);
  std::memcpy(out + 24, key, 32);
  write_u32_le(out + 56, crc32(out, 56));
}

bool decode_credentials(const uint8_t in[kCredentialBytes], uint8_t salt[16], uint8_t key[32], bool* is_default) {
  if (in[0] != 'W' || in[1] != 'A' || in[2] != 'U' || in[3] != 'T' ||
      in[4] != kCredentialVersion || in[5] > 1 || in[6] != 0 || in[7] != 0 ||
      read_u32_le(in + 56) != crc32(in, 56)) return false;
  std::memcpy(salt, in + 8, 16);
  std::memcpy(key, in + 24, 32);
  *is_default = in[5] != 0;
  return true;
}

enum class LoadCredentials : uint8_t { missing, current, legacy, invalid, unavailable };

LoadCredentials load_credentials() {
  nvs_handle_t handle{};
  if (nvs_open("orcsdr_wauth", NVS_READWRITE, &handle) != ESP_OK) return LoadCredentials::unavailable;

  auto read_blob = [handle](const char* name, void* out, size_t expected, bool* present) {
    size_t size = 0;
    const esp_err_t query = nvs_get_blob(handle, name, nullptr, &size);
    if (query == ESP_ERR_NVS_NOT_FOUND) { *present = false; return ESP_OK; }
    if (query != ESP_OK) return query;
    *present = true;
    if (size != expected) return ESP_FAIL;
    return nvs_get_blob(handle, name, out, &size);
  };

  uint8_t record[kCredentialBytes]{};
  bool has_record = false;
  esp_err_t error = read_blob("credential", record, sizeof(record), &has_record);
  if (error != ESP_OK) {
    nvs_close(handle);
    return LoadCredentials::invalid;
  }
  if (has_record) {
    bool is_default = true;
    uint8_t salt[16], key[32];
    const bool valid = decode_credentials(record, salt, key, &is_default);
    if (valid) {
      std::memcpy(g_salt, salt, sizeof(g_salt));
      std::memcpy(g_key, key, sizeof(g_key));
      g_default = is_default;
    }
    nvs_close(handle);
    return valid ? LoadCredentials::current : LoadCredentials::invalid;
  }

  uint8_t salt[16], key[32], is_default = 1;
  bool have_salt = false, have_key = false;
  const esp_err_t salt_error = read_blob("salt", salt, sizeof(salt), &have_salt);
  const esp_err_t key_error = read_blob("key", key, sizeof(key), &have_key);
  if (salt_error != ESP_OK || key_error != ESP_OK) {
    nvs_close(handle);
    return LoadCredentials::invalid;
  }
  if (!have_salt && !have_key) {
    const esp_err_t default_error = nvs_get_u8(handle, "def", &is_default);
    nvs_close(handle);
    return default_error == ESP_ERR_NVS_NOT_FOUND ? LoadCredentials::missing : LoadCredentials::invalid;
  }
  if (!have_salt || !have_key) {
    nvs_close(handle);
    return LoadCredentials::invalid;
  }
  const esp_err_t default_error = nvs_get_u8(handle, "def", &is_default);
  nvs_close(handle);
  if (default_error != ESP_OK && default_error != ESP_ERR_NVS_NOT_FOUND) return LoadCredentials::invalid;
  if (is_default > 1) return LoadCredentials::invalid;
  std::memcpy(g_salt, salt, sizeof(g_salt));
  std::memcpy(g_key, key, sizeof(g_key));
  g_default = is_default != 0;
  return LoadCredentials::legacy;
}

// Keep one authoritative versioned record. ESP-IDF 5.5's simple NVS handle writes
// each set_blob immediately; commit() is not a multi-key transaction. Legacy keys
// are read only for migration and intentionally remain unchanged after migration.
bool store_credentials(const uint8_t salt[16], const uint8_t key[32], bool is_default) {
  uint8_t record[kCredentialBytes];
  encode_credentials(salt, key, is_default, record);
  nvs_handle_t handle{};
  if (nvs_open("orcsdr_wauth", NVS_READWRITE, &handle) != ESP_OK) return false;
  const esp_err_t error = nvs_set_blob(handle, "credential", record, sizeof(record));
  nvs_close(handle);
  return error == ESP_OK;
}

void end_all_sessions() {
  for (Session& s : g_sessions) s = Session{};
}

Peer* peer_slot(uint32_t ip, int64_t now) {
  Peer* oldest = &g_peers[0];
  for (Peer& p : g_peers) {
    if (p.ip == ip && p.last_us) return &p;
    if (p.last_us < oldest->last_us) oldest = &p;
  }
  *oldest = Peer{};
  oldest->ip = ip;
  oldest->last_us = now;
  return oldest;
}
}  // namespace

void init() {
  if (g_ready) return;
  // Reuse the mutex on failed-init retries; replacing it would strand any waiter
  // on the old handle and leak another semaphore on each retry.
  if (!g_mutex) {
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) return;
  }
  Lock lock;
  const LoadCredentials loaded = load_credentials();
  if (loaded == LoadCredentials::invalid || loaded == LoadCredentials::unavailable) {
    ESP_LOGE(TAG, "stored web credentials are invalid or unavailable; web login stays disabled");
    return;
  }
  if (loaded == LoadCredentials::missing) {
    esp_fill_random(g_salt, sizeof(g_salt));
    g_default = true;
    if (!derive(kFactoryPassword, g_salt, g_key)) {
      ESP_LOGE(TAG, "key derivation failed; web login stays unavailable");
      return;
    }
    if (!store_credentials(g_salt, g_key, g_default)) {
      ESP_LOGE(TAG, "could not persist initial web credentials; web login stays disabled");
      return;
    }
    ESP_LOGW(TAG, "WEB_LOGIN_FACTORY_PASSWORD created: user admin, factory password; change it through a privileged local interface");
  } else if (loaded == LoadCredentials::legacy && !store_credentials(g_salt, g_key, g_default)) {
    // The legacy keys remain untouched because the attempted migration was not committed.
    ESP_LOGW(TAG, "could not migrate legacy web credentials; continuing with the loaded credentials");
  }
  if (g_default) {
    ESP_LOGW(TAG, "WEB_LOGIN_FACTORY_PASSWORD in use: change it through a privileged local interface");
  }
  g_ready = true;
}

bool default_password() {
  if (!g_mutex) return true;
  Lock lock;
  return g_default;
}

bool set_password(const char* password) {
  // This entry point is restricted to a privileged local caller. It also permits
  // that caller to recover from corrupt/unavailable persisted credentials.
  if (!g_mutex || !password) return false;
  const size_t n = std::strlen(password);
  if (n < 8 || n > 64 || std::strcmp(password, kFactoryPassword) == 0) return false;
  uint8_t salt[16], key[32];
  esp_fill_random(salt, sizeof(salt));
  if (!derive(password, salt, key)) return false;
  Lock lock;
  if (!store_credentials(salt, key, false)) return false;
  std::memcpy(g_salt, salt, sizeof(g_salt));
  std::memcpy(g_key, key, sizeof(g_key));
  g_default = false;
  end_all_sessions();
  for (Nonce& nonce : g_nonces) nonce = Nonce{};
  for (Peer& peer : g_peers) peer = Peer{};
  g_ready = true;
  return true;
}

void salt_hex(char out[33]) {
  Lock lock;
  to_hex(g_salt, sizeof(g_salt), out);
}

bool issue_challenge(char out_hex[33]) {
  if (!g_ready) return false;
  Lock lock;
  const int64_t now = now_us();
  // When every slot holds an unexpired nonce the one closest to expiring is replaced: refusing would let anyone who asks for four challenges
  // keep the owner from logging in. A login that used the replaced nonce is answered bad_nonce and simply asks again.
  Nonce* slot = &g_nonces[0];
  for (Nonce& n : g_nonces) {
    if (n.expires_us && n.expires_us <= now) n.expires_us = 0;
    if (n.expires_us < slot->expires_us) slot = &n;
  }
  esp_fill_random(slot->value, sizeof(slot->value));
  slot->expires_us = now + kNonceLifeUs;
  to_hex(slot->value, sizeof(slot->value), out_hex);
  return true;
}

Login login(uint32_t peer, const char* nonce_hex, const char* proof_hex, LoginOut& out) {
  uint8_t nonce[16], proof[32];
  if (!g_ready || !from_hex(nonce_hex, nonce, sizeof(nonce)) || !from_hex(proof_hex, proof, sizeof(proof))) return Login::bad_request;
  Lock lock;
  const int64_t now = now_us();
  Peer* p = peer_slot(peer, now);
  if (p->until_us > now) {
    out.retry_s = static_cast<uint32_t>((p->until_us - now + 999999) / 1000000);
    return Login::throttled;
  }
  // The nonce is consumed whatever happens next.
  Nonce* found = nullptr;
  for (Nonce& n : g_nonces)
    if (n.expires_us > now && equal(n.value, nonce, sizeof(nonce))) found = &n;
  if (!found) return Login::bad_nonce;
  found->expires_us = 0;

  uint8_t expected[32];
  if (!labelled(g_key, "login|", nonce, "|admin", expected) || !equal(expected, proof, sizeof(proof))) {
    if (p->fails < 6) ++p->fails;
    p->until_us = now + (1LL << (p->fails - 1)) * 1000000LL;  // 1 s, 2 s, 4 s ... 30 s
    if (p->until_us - now > 30000000LL) p->until_us = now + 30000000LL;
    p->last_us = now;
    ESP_LOGW(TAG, "WEB_LOGIN_FAILED peer=%08x fails=%u", static_cast<unsigned>(peer), p->fails);
    return Login::bad_proof;
  }
  p->fails = 0;
  p->until_us = 0;
  p->last_us = now;

  Session* slot = nullptr;
  for (Session& s : g_sessions) {
    if (s.used && (now - s.last_us > kIdleUs || now - s.created_us > kHardUs)) s.used = false;
    if (!s.used && !slot) slot = &s;
  }
  if (!slot) {  // every slot is live: the one idle the longest makes room, so a correct password is never turned away (remembered browsers come and go)
    slot = &g_sessions[0];
    for (Session& s : g_sessions)
      if (s.last_us < slot->last_us) slot = &s;
    ESP_LOGW(TAG, "WEB_SESSION_EVICTED the least recently used session made room for a new login");
  }
  uint8_t device[32];
  if (!labelled(g_key, "device|", nonce, nullptr, device) || !labelled(g_key, "session|", nonce, nullptr, slot->key)) return Login::bad_request;
  esp_fill_random(slot->id, sizeof(slot->id));
  slot->last_seq = 0;
  slot->created_us = slot->last_us = now;
  slot->used = true;
  to_hex(slot->id, sizeof(slot->id), out.sid);
  to_hex(device, sizeof(device), out.device_proof);
  ESP_LOGI(TAG, "WEB_LOGIN_OK peer=%08x default_password=%d", static_cast<unsigned>(peer), g_default);
  return Login::ok;
}

void logout(const char* sid) {
  uint8_t id[8];
  if (!from_hex(sid, id, sizeof(id))) return;
  Lock lock;
  for (Session& s : g_sessions)
    if (s.used && equal(s.id, id, sizeof(id))) s = Session{};
}

Verify verify(const char* sid, const char* seq, const char* sig_hex, const char* method, const char* target, const uint8_t* body, size_t body_len) {
  uint8_t id[8], sig[32];
  if (!seq || !method || !target || !from_hex(sid, id, sizeof(id)) || !from_hex(sig_hex, sig, sizeof(sig))) return Verify::bad_request;
  char* end = nullptr;
  if (!*seq || std::strlen(seq) > 15) return Verify::bad_request;
  const unsigned long long number = std::strtoull(seq, &end, 10);
  if (*end != '\0') return Verify::bad_request;

  uint8_t body_hash[32];
  if (mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), body ? body : reinterpret_cast<const uint8_t*>(""), body_len, body_hash) != 0) return Verify::bad_request;
  char body_hex[65];
  to_hex(body_hash, sizeof(body_hash), body_hex);

  Lock lock;
  const int64_t now = now_us();
  Session* s = nullptr;
  for (Session& c : g_sessions)
    if (c.used && equal(c.id, id, sizeof(id))) s = &c;
  if (!s) return Verify::no_session;
  if (now - s->last_us > kIdleUs || now - s->created_us > kHardUs) {
    *s = Session{};
    return Verify::expired;
  }
  if (number <= s->last_seq) return Verify::bad_seq;

  // seq "\n" METHOD "\n" target "\n" hex(SHA256(body))
  char message[360];
  const int n = std::snprintf(message, sizeof(message), "%s\n%s\n%s\n%s", seq, method, target, body_hex);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(message)) return Verify::bad_request;
  uint8_t expected[32];
  if (!hmac(s->key, sizeof(s->key), reinterpret_cast<const uint8_t*>(message), static_cast<size_t>(n), expected) || !equal(expected, sig, sizeof(sig))) return Verify::bad_sig;
  s->last_seq = number;
  s->last_us = now;
  return Verify::ok;
}

int active_sessions() {
  if (!g_ready) return 0;
  Lock lock;
  const int64_t now = now_us();
  int n = 0;
  for (const Session& s : g_sessions) n += s.used && now - s.last_us <= kIdleUs && now - s.created_us <= kHardUs;
  return n;
}

#if defined(ORCSDR_WEB_AUTH_HOST_TEST)
void reset_runtime_for_test() {
  g_ready = false;
  g_mutex = nullptr;
  std::memset(g_key, 0, sizeof(g_key));
  std::memset(g_salt, 0, sizeof(g_salt));
  g_default = true;
  for (Nonce& nonce : g_nonces) nonce = Nonce{};
  for (Session& session : g_sessions) session = Session{};
  for (Peer& peer : g_peers) peer = Peer{};
}
#endif

}  // namespace web_auth
