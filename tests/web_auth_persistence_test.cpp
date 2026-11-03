#include "web_auth.hpp"
#include "nvs_test_support.hpp"
#include "crypto_test_support.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace web_auth { void reset_runtime_for_test(); }

namespace {
uint32_t g_peer = 0x01020304;
void to_hex(const uint8_t* in, size_t n, char* out) {
  static constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    out[i * 2] = digits[in[i] >> 4];
    out[i * 2 + 1] = digits[in[i] & 15];
  }
  out[n * 2] = '\0';
}

bool login_with_password(const char* password) {
  char nonce_hex[33], salt_hex[33];
  if (!web_auth::issue_challenge(nonce_hex)) return false;
  web_auth::salt_hex(salt_hex);
  uint8_t nonce[16], salt[16], key[32], proof[32];
  auto nibble = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    return static_cast<uint8_t>((c & 0x5f) - 'A' + 10);
  };
  for (size_t i = 0; i < 16; ++i) {
    nonce[i] = static_cast<uint8_t>((nibble(nonce_hex[i * 2]) << 4) | nibble(nonce_hex[i * 2 + 1]));
    salt[i] = static_cast<uint8_t>((nibble(salt_hex[i * 2]) << 4) | nibble(salt_hex[i * 2 + 1]));
  }
  test_pbkdf2(reinterpret_cast<const unsigned char*>(password), std::strlen(password), salt, sizeof(salt), key);
  uint8_t message[37]{};
  std::memcpy(message, "login|", 6);
  std::memcpy(message + 6, nonce, 16);
  std::memcpy(message + 22, "|admin", 6);
  test_hmac(key, sizeof(key), message, 28, proof);
  char proof_hex[65];
  to_hex(proof, sizeof(proof), proof_hex);
  web_auth::LoginOut out{};
  return web_auth::login(g_peer++, nonce_hex, proof_hex, out) == web_auth::Login::ok;
}
}

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--legacy") == 0) {
    uint8_t salt[16], key[32];
    for (size_t i = 0; i < sizeof(salt); ++i) salt[i] = static_cast<uint8_t>(0x40 + i);
    const char* legacy_password = "legacy-pass-123";
    test_pbkdf2(reinterpret_cast<const unsigned char*>(legacy_password), std::strlen(legacy_password), salt, sizeof(salt), key);
    assert(web_auth_test_store::seed_blob("salt", salt, sizeof(salt)));
    assert(web_auth_test_store::seed_blob("key", key, sizeof(key)));
    assert(web_auth_test_store::seed_u8("def", 0));
    web_auth::init();
    assert(!web_auth::default_password());
    assert(web_auth_test_store::contains("credential"));
    assert(login_with_password(legacy_password));
    assert(web_auth::set_password("migrated-pass-123"));
    assert(!login_with_password(legacy_password));
    assert(login_with_password("migrated-pass-123"));
    std::puts("P4 web auth legacy credential migration PASS (crypto is stubbed)");
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "--corrupt") == 0) {
    const uint8_t corrupt[] = {0x57, 0x41, 0x55, 0x54};
    assert(web_auth_test_store::seed_blob("credential", corrupt, sizeof(corrupt)));
    web_auth::init();
    char nonce[33];
    assert(!web_auth::issue_challenge(nonce));
    web_auth::init();  // A failed retry reuses the one mutex.
    assert(web_auth_test_store::mutex_create_count() == 1);
    assert(web_auth::set_password("recovery-pass-123")); // privileged local recovery path
    assert(web_auth::issue_challenge(nonce));
    assert(login_with_password("recovery-pass-123"));
    std::puts("P4 web auth corrupt credential fails closed PASS (crypto is stubbed)");
    return 0;
  }
  if (argc > 1 && (std::strcmp(argv[1], "--write-before-reboot") == 0 ||
                   std::strcmp(argv[1], "--write-after-reboot") == 0)) {
    const bool after = std::strcmp(argv[1], "--write-after-reboot") == 0;
    web_auth::init();
    const char* old_password = "password";
    const char* new_password = "new-password-123";
    if (after) web_auth_test_store::fail_next_write_after();
    else web_auth_test_store::fail_next_write_before();
    assert(!web_auth::set_password(new_password));
    assert(web_auth::default_password()); // in-RAM state changes only after a successful return
    assert(login_with_password(old_password));
    web_auth::reset_runtime_for_test();
    web_auth::init();
    const bool new_is_durable = after;
    assert(login_with_password(new_is_durable ? new_password : old_password));
    assert(!login_with_password(new_is_durable ? old_password : new_password));
    std::puts(after ? "P4 web auth write-error reboot resolves to complete new record PASS (crypto is stubbed)"
                    : "P4 web auth pre-write failure reboot retains complete old record PASS (crypto is stubbed)");
    return 0;
  }
  web_auth::init();
  assert(web_auth::default_password());
  assert(login_with_password("password"));
  assert(web_auth::active_sessions() == 1);

  // NVS writes through each set_blob. A failed write before persistence leaves
  // both in-memory credentials and sessions on the prior credential.
  const size_t writes_before_failure = web_auth_test_store::write_count();
  web_auth_test_store::fail_next_write_before();
  assert(!web_auth::set_password("new-password-123"));
  assert(web_auth::default_password());
  assert(web_auth::active_sessions() == 1);
  assert(login_with_password("password"));
  assert(!login_with_password("new-password-123"));

  const size_t writes_before_success = web_auth_test_store::write_count();
  assert(web_auth::set_password("new-password-123"));
  assert(web_auth_test_store::write_count() == writes_before_success + 1);
  assert(web_auth_test_store::write_count() == writes_before_failure + 2);
  assert(!web_auth::default_password());
  assert(web_auth::active_sessions() == 0);
  assert(web_auth_test_store::contains("credential"));
  assert(!web_auth_test_store::contains("salt"));
  assert(!web_auth_test_store::contains("key"));
  assert(!web_auth_test_store::contains("def"));
  assert(!login_with_password("password"));
  assert(login_with_password("new-password-123"));
  std::puts("P4 web auth immediate NVS write failure/success boundary PASS (crypto is stubbed)");
}
