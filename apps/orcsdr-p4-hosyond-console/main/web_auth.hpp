#pragma once
// Login and signed requests for the control tier of the web layer (docs/hardware/WEB_LAYER_DESIGN.md section 3 and 14).
// Plain HTTP, so no secret and no reusable token crosses the wire: the browser proves it knows the password with an HMAC over a
// server nonce, then signs every control request with a session key that both ends derive and neither sends.
//   key         = PBKDF2-HMAC-SHA256(password, salt, kIterations, 32 bytes)      (the device stores key and salt, never the password)
//   login proof = HMAC(key, "login|" + nonce + "|admin")
//   device      = HMAC(key, "device|" + nonce)             (lets the browser check it is talking to the device that knows the key)
//   session key = HMAC(key, "session|" + nonce)
//   request sig = HMAC(session key, seq + "\n" + METHOD + "\n" + path-and-query + "\n" + hex(SHA256(body)))
// The user name is always "admin". The factory password is "password" (owner decision); default_password() stays true until it is changed
// over the serial console (RTL_WEB PASSWORD), and the page is told so.
#include <cstddef>
#include <cstdint>

namespace web_auth {

constexpr uint32_t kIterations = 10000;
constexpr int kMaxSessions = 4;  // a full table evicts the least recently used session, so this is how many browsers stay logged in at once

void init();                              // loads the stored key, or creates the factory one
bool default_password();                  // true while the factory password is in use
bool set_password(const char* password);  // 8..64 characters, not the factory one; ends every session. Serial console only.
void salt_hex(char out[33]);              // the salt the browser needs for the key derivation

// Issues a single-use 16-byte nonce valid for 30 s (hex, 32 chars + NUL). When all four slots are in use the oldest is replaced.
bool issue_challenge(char out_hex[33]);

enum class Login : uint8_t { ok, bad_request, bad_nonce, bad_proof, throttled, busy };
struct LoginOut {
  char sid[17];            // session id, 8 bytes hex
  char device_proof[65];   // hex
  uint32_t retry_s = 0;    // throttled: seconds until another attempt is accepted
};
// `peer` is the client address (any stable 32-bit value; 0 is allowed). The nonce is consumed whatever the outcome.
Login login(uint32_t peer, const char* nonce_hex, const char* proof_hex, LoginOut& out);
void logout(const char* sid);

enum class Verify : uint8_t { ok, bad_request, no_session, expired, bad_seq, bad_sig };
Verify verify(const char* sid, const char* seq, const char* sig_hex, const char* method, const char* target, const uint8_t* body, size_t body_len);

int active_sessions();

}  // namespace web_auth
