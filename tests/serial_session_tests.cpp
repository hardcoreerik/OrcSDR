// Host tests for the session protocol moved out of main.cpp (serial_session.cpp): PAIR, AUTH, PING, PREPARE_FLASH,
// SET_WIFI, INSTALL_STATUS, ROTATE_KEY, TEST_PRESSURE, PRESSURE_ACK, ACK. Expected behaviour is typed from the code
// this replaced. The proofs are real HMAC-SHA256 values: this file carries its own SHA-256, checked first against
// RFC 4231 test case 2, so a host tool written from docs/API_SERIAL_CLI.md computes the same bytes.
// Build with g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Iapps/orcsdr-tab5/ui, from the
// repository root: tests/serial_session_tests.cpp apps/orcsdr-tab5/ui/serial_session.cpp
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "serial_session.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

// ---- SHA-256 / HMAC (FIPS 180-4, RFC 2104) ----
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

std::vector<uint8_t> sha256(const std::vector<uint8_t>& message) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::vector<uint8_t> m = message;
  const uint64_t bits = static_cast<uint64_t>(message.size()) * 8;
  m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 7; i >= 0; --i) m.push_back(static_cast<uint8_t>(bits >> (8 * i)));
  for (size_t block = 0; block < m.size(); block += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t(m[block + 4 * i]) << 24) | (uint32_t(m[block + 4 * i + 1]) << 16) |
             (uint32_t(m[block + 4 * i + 2]) << 8) | uint32_t(m[block + 4 * i + 3]);
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  std::vector<uint8_t> out;
  for (uint32_t v : h)
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  return out;
}

std::vector<uint8_t> hmac(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> k = key.size() > 64 ? sha256(key) : key;
  k.resize(64, 0);
  std::vector<uint8_t> inner, outer;
  for (uint8_t b : k) { inner.push_back(b ^ 0x36); outer.push_back(b ^ 0x5c); }
  inner.insert(inner.end(), data.begin(), data.end());
  const std::vector<uint8_t> inner_hash = sha256(inner);
  outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
  return sha256(outer);
}

std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

std::string hex(const std::vector<uint8_t>& v) {
  std::string out;
  char two[3];
  for (uint8_t b : v) { std::snprintf(two, sizeof(two), "%02x", b); out += two; }
  return out;
}

int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// main.cpp's decode_hex: exactly 2*size hex digits.
bool decode_hex(const char* text, uint8_t* out, size_t size) {
  if (strlen(text) != 2 * size) return false;
  for (size_t i = 0; i < size; ++i) {
    const int hi = nibble(text[2 * i]), lo = nibble(text[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi * 16 + lo);
  }
  return true;
}

bool decode_hex_text(const char* text, char* out, size_t size) {
  const size_t n = strlen(text);
  if (n % 2 != 0 || n / 2 >= size) return false;
  if (!decode_hex(text, reinterpret_cast<uint8_t*>(out), n / 2)) return false;
  out[n / 2] = '\0';
  return true;
}

// ---- device state the hooks reach ----
std::string g_out;
std::vector<std::string> g_calls;
bool g_paired = false;
uint8_t g_key[32];
bool g_authenticated = false;
bool g_hmac_fails = false;
uint32_t g_revision = 0, g_dropped = 3, g_next_sequence = 10;
std::string g_ssid, g_password;

const orcsdr::serial_session::Hooks kHooks = [] {
  orcsdr::serial_session::Hooks h;
  h.write = [](const char* t) { g_out += t; };
  h.write_line = [](const char* t) { g_out += t; g_out += "\n"; };
  h.write_hex = [](const uint8_t* v, size_t n) { g_out += hex(std::vector<uint8_t>(v, v + n)); };
  h.decode_hex = decode_hex;
  h.decode_hex_text = decode_hex_text;
  h.hmac = [](const uint8_t* data, size_t n, uint8_t out[32]) {
    if (g_hmac_fails) return false;
    const auto mac = hmac(std::vector<uint8_t>(g_key, g_key + 32), std::vector<uint8_t>(data, data + n));
    memcpy(out, mac.data(), 32);
    return true;
  };
  h.paired = [] { return g_paired; };
  h.pairing_key = []() -> const uint8_t* { return g_key; };
  h.store_pairing_key = [](const uint8_t* k) { memcpy(g_key, k, 32); g_paired = true; g_calls.push_back("store_key"); };
  h.set_authenticated = [](bool on) { g_authenticated = on; g_calls.push_back(on ? "auth on" : "auth off"); };
  h.session_started = [] { g_calls.push_back("session_started"); };
  h.session_ended = [] { g_calls.push_back("session_ended"); };
  h.ping = [] { g_calls.push_back("ping"); };
  h.journal = [](const char* e) { g_calls.push_back(std::string("journal ") + e); };
  h.show_status = [](const char* t, orcsdr::serial_session::Tone tone) {
    g_calls.push_back(std::string(tone == orcsdr::serial_session::Tone::approved ? "cyan " : "yellow ") + t);
  };
  h.emit_identity = [] { g_calls.push_back("identity"); };
  h.emit_pending_journal = [] { g_calls.push_back("pending"); };
  h.store_wifi = [](const char* s, const char* p) { g_ssid = s; g_password = p; g_calls.push_back("store_wifi"); };
  h.start_wifi = [] { g_calls.push_back("start_wifi"); };
  h.workflow_revision = [] { return g_revision; };
  h.install_workflow = [](uint32_t r, uint16_t m) {
    g_revision = r;
    g_calls.push_back("workflow " + std::to_string(r) + " " + std::to_string(m));
  };
  h.dropped_events = [] { return g_dropped; };
  h.clear_dropped_events = [] { g_dropped = 0; g_calls.push_back("clear_dropped"); };
  h.next_sequence = [] { return g_next_sequence; };
  h.acknowledge = [](uint32_t s) { g_calls.push_back("ack " + std::to_string(s)); };
  return h;
}();

bool run(const std::string& line, bool authenticated) {
  g_out.clear();
  g_calls.clear();
  std::vector<char> buffer(line.begin(), line.end());
  buffer.push_back('\0');
  return orcsdr::serial_session::handle_command(buffer.data(), authenticated, kHooks);
}

std::vector<uint8_t> key_of(uint8_t seed) {
  std::vector<uint8_t> k(32);
  for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(seed + i);
  return k;
}

std::string calls() {
  std::string out;
  for (const auto& c : g_calls) out += c + ";";
  return out;
}

}  // namespace

int main() {
  // The test's HMAC first: RFC 4231 test case 2.
  CHECK(hex(hmac(bytes("Jefe"), bytes("what do ya want for nothing?"))) ==
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

  const std::vector<uint8_t> key = key_of(1);
  const std::vector<uint8_t> other = key_of(100);

  // PAIR: bad hex, first use stores, same key OK, another key LOCKED and not stored.
  CHECK(run("PAIR 12zz", false) && g_out == "PAIR_INVALID\n" && !g_paired);
  CHECK(run("PAIR " + hex(key), false) && g_out == "PAIR_OK\n" && g_paired);
  CHECK(calls() == "store_key;journal paired;");
  CHECK(run("PAIR " + hex(key), false) && g_out == "PAIR_OK\n" && calls().empty());
  const std::string locked = std::string("PAIR_LOCKED\n") + orcsdr::serial_session::kPairLockedHint + "\n";
  CHECK(run("PAIR " + hex(other), false) && g_out == locked && calls().empty());
  CHECK(g_out.rfind("PAIR_LOCKED\n", 0) == 0);   // hosts reading one line still see PAIR_LOCKED
  CHECK(memcmp(g_key, key.data(), 32) == 0);

  // AUTH.
  const std::vector<uint8_t> nonce = key_of(7);   // first 16 bytes used
  const std::vector<uint8_t> n16(nonce.begin(), nonce.begin() + 16);
  std::vector<uint8_t> host_in = bytes("host");
  host_in.insert(host_in.end(), n16.begin(), n16.end());
  std::vector<uint8_t> device_in = bytes("device");
  device_in.insert(device_in.end(), n16.begin(), n16.end());
  const std::string good_auth = "AUTH " + hex(n16) + " " + hex(hmac(key, host_in));

  CHECK(run("AUTH " + hex(n16), false) && g_out == "AUTH_INVALID\n");
  CHECK(run("AUTH 00 " + hex(hmac(key, host_in)), false) && g_out == "AUTH_INVALID\n");
  CHECK(run("AUTH " + hex(n16) + " " + hex(hmac(other, host_in)), false) && g_out == "AUTH_DENIED\n");
  CHECK(!g_authenticated && calls().empty());
  g_hmac_fails = true;
  CHECK(run(good_auth, false) && g_out == "AUTH_ERROR\n" && !g_authenticated);
  g_hmac_fails = false;
  CHECK(run(good_auth, false) && g_out == "AUTH_OK " + hex(hmac(key, device_in)) + "\n");
  CHECK(g_authenticated && calls() == "auth on;session_started;");
  g_paired = false;
  CHECK(!run(good_auth, false) && g_out.empty());   // not paired: not this route
  g_paired = true;

  // Every session command is silent and unclaimed when signed out.
  for (const char* line : {"PING", "PREPARE_FLASH 00", "SET_WIFI a b c", "INSTALL_STATUS 1 2 3",
                           "ROTATE_KEY a b", "TEST_PRESSURE", "PRESSURE_ACK", "ACK 1"}) {
    CHECK(!run(line, false) && g_out.empty() && calls().empty());
  }
  CHECK(!run("PINGX", true) && !run("ACK", true) && !run("pair 00", false));

  CHECK(run("PING", true) && g_out.empty() && calls() == "ping;");

  // PREPARE_FLASH.
  const std::string digest = hex(key_of(50));
  CHECK(run("PREPARE_FLASH 1234", true) && g_out == "FLASH_INVALID\n" && calls().empty());
  CHECK(run("PREPARE_FLASH " + digest, true) && g_out == "FLASH_READY " + digest + "\n");
  CHECK(calls() == "cyan Approved firmware update starting;journal firmware_approved;identity;pending;");

  // SET_WIFI: the signature covers "wifi|<ssid hex>|<password hex>".
  const std::string ssid_hex = hex(bytes("OrcNet")), pass_hex = hex(bytes("secret pw"));
  const std::string wifi_sig = hex(hmac(key, bytes("wifi|" + ssid_hex + "|" + pass_hex)));
  CHECK(run("SET_WIFI " + ssid_hex, true) && g_out == "WIFI_INVALID\n");
  CHECK(run("SET_WIFI " + ssid_hex + " " + pass_hex, true) && g_out == "WIFI_INVALID\n");
  CHECK(run("SET_WIFI " + ssid_hex + " " + pass_hex + " " + hex(hmac(other, bytes("x"))), true) &&
        g_out == "WIFI_INVALID\n" && calls().empty());
  CHECK(run("SET_WIFI  " + pass_hex + " " + hex(hmac(key, bytes("wifi||" + pass_hex))), true) &&
        g_out == "WIFI_INVALID\n");   // empty SSID
  CHECK(run("SET_WIFI " + std::string(66, '4') + " " + pass_hex + " " + wifi_sig, true) &&
        g_out == "WIFI_INVALID\n");   // 33-byte SSID: too long
  CHECK(run("SET_WIFI " + ssid_hex + " " + pass_hex + " " + wifi_sig, true) && g_out == "WIFI_CONFIGURED\n");
  CHECK(g_ssid == "OrcNet" && g_password == "secret pw");
  CHECK(calls() == "store_wifi;journal wifi_configured;start_wifi;identity;pending;");

  // INSTALL_STATUS: signature over "workflow|<rev>|<max>"; zero, too large and stale are refused.
  auto workflow_line = [&](const std::string& rev, const std::string& max) {
    return "INSTALL_STATUS " + rev + " " + max + " " + hex(hmac(key, bytes("workflow|" + rev + "|" + max)));
  };
  CHECK(run("INSTALL_STATUS 5", true) && g_out == "WORKFLOW_INVALID\n");
  CHECK(run(workflow_line("0", "3"), true) && g_out == "WORKFLOW_INVALID\n");
  CHECK(run(workflow_line("5", "65536"), true) && g_out == "WORKFLOW_INVALID\n");
  CHECK(run("INSTALL_STATUS 5 3 " + hex(hmac(other, bytes("workflow|5|3"))), true) && g_out == "WORKFLOW_INVALID\n");
  CHECK(run(workflow_line("5", "3"), true) && g_out == "WORKFLOW_OK 5\n");
  CHECK(calls() == "workflow 5 3;journal workflow_installed;identity;pending;");
  CHECK(run(workflow_line("5", "4"), true) && g_out == "WORKFLOW_STALE\n" && calls().empty());

  // TEST_PRESSURE / PRESSURE_ACK / ACK.
  CHECK(run("TEST_PRESSURE", true) && g_out == "PRESSURE_OK 3\n" && g_calls.size() == 12);
  CHECK(run("PRESSURE_ACK", true) && g_out == "PRESSURE_CLEARED\n" && calls() == "clear_dropped;identity;");
  CHECK(run("ACK 10", true) && calls() == "ack 10;");
  CHECK(run("ACK 11", true) && calls().empty());

  // ROTATE_KEY: signature over "rotate|<new key hex>"; then the session ends.
  const std::string new_key = hex(other);
  CHECK(run("ROTATE_KEY " + new_key, true) && g_out == "ROTATE_INVALID\n");
  CHECK(run("ROTATE_KEY 00 " + hex(hmac(key, bytes("rotate|00"))), true) && g_out == "ROTATE_INVALID\n");
  CHECK(run("ROTATE_KEY " + new_key + " " + hex(hmac(other, bytes("rotate|" + new_key))), true) &&
        g_out == "ROTATE_INVALID\n" && memcmp(g_key, key.data(), 32) == 0);
  CHECK(run("ROTATE_KEY " + new_key + " " + hex(hmac(key, bytes("rotate|" + new_key))), true) &&
        g_out == "KEY_ROTATED\n");
  CHECK(memcmp(g_key, other.data(), 32) == 0 && !g_authenticated);
  CHECK(calls() ==
        "store_key;journal credential_rotated;auth off;session_ended;yellow Credential rotated - host reconnect required;");
  CHECK(run("PAIR " + hex(other), false) && g_out == "PAIR_OK\n");
  CHECK(run("PAIR " + hex(key), false) && g_out == locked);

  if (failures == 0) std::printf("serial_session: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
