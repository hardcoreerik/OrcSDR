#pragma once
// The host session protocol: PAIR, AUTH, PING, PREPARE_FLASH, SET_WIFI, INSTALL_STATUS, ROTATE_KEY, TEST_PRESSURE,
// PRESSURE_ACK and ACK.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). Every
// route, its matching, its sign-in rule and every reply are the ones the dispatcher had, byte for byte:
//   PAIR <key hex>            trust on first use: an unpaired unit stores the key; then PAIR_OK, PAIR_LOCKED (followed
//                             by a PAIR_HINT line) or PAIR_INVALID. No sign-in.
//   AUTH <nonce> <proof>      only once paired: proof = HMAC-SHA256(key, "host"+nonce); the reply signs
//                             "device"+nonce. Starts the session.
//   the rest                  only inside a session; nothing is printed when signed out. SET_WIFI, INSTALL_STATUS and
//                             ROTATE_KEY also carry an HMAC of their own values.
// The key, the stored settings and the journal stay in main.cpp; this file only reads the lines and builds the replies.
// The handler takes a writable line because AUTH, SET_WIFI, INSTALL_STATUS and ROTATE_KEY split it in place.
#include <cstddef>
#include <cstdint>

namespace orcsdr::serial_session {

constexpr size_t kPairingKeySize = 32;     // bytes of the shared key
constexpr size_t kWifiSsidSize = 33;       // including the terminator, as main.cpp's wifi_ssid
constexpr size_t kWifiPasswordSize = 64;   // including the terminator, as main.cpp's wifi_password

enum class Tone { approved, warning };     // the session banner colour: cyan, yellow

// Printed on the line after PAIR_LOCKED: where the key lives and how to clear it without erasing the flash.
constexpr const char* kPairLockedHint =
    "PAIR_HINT another computer's key is stored. Use that key file (default .orclink/ui-doc.key), or clear it on "
    "the device: Settings > System > FORGET COMPUTER. See docs/API_SERIAL_CLI.md (Auth model).";

// Every pointer must be set.
struct Hooks {
  void (*write)(const char* text) = nullptr;                          // as given (Serial.print)
  void (*write_line)(const char* text) = nullptr;                     // then the line ending (Serial.println)
  void (*write_hex)(const uint8_t* value, size_t size) = nullptr;     // lower-case hex, no line ending
  bool (*decode_hex)(const char* text, uint8_t* out, size_t size) = nullptr;       // exactly `size` bytes
  bool (*decode_hex_text)(const char* text, char* out, size_t size) = nullptr;     // hex to a C string
  // HMAC-SHA256 keyed with the stored pairing key; false when the hash could not be computed.
  bool (*hmac)(const uint8_t* data, size_t size, uint8_t out[32]) = nullptr;
  bool (*paired)() = nullptr;
  const uint8_t* (*pairing_key)() = nullptr;                          // kPairingKeySize bytes
  void (*store_pairing_key)(const uint8_t* key) = nullptr;            // copy, save "pair_key", mark paired
  void (*set_authenticated)(bool on) = nullptr;
  void (*session_started)() = nullptr;     // after AUTH_OK: end the splash, restart the ping clock, go online
  void (*session_ended)() = nullptr;       // after KEY_ROTATED: re-arm the offline transition, restart the clock
  void (*ping)() = nullptr;                // restart the ping clock
  void (*journal)(const char* event) = nullptr;
  void (*show_status)(const char* text, Tone tone) = nullptr;
  void (*emit_identity)() = nullptr;
  void (*emit_pending_journal)() = nullptr;
  void (*store_wifi)(const char* ssid, const char* password) = nullptr;   // settings + first profile, configured
  void (*start_wifi)() = nullptr;
  uint32_t (*workflow_revision)() = nullptr;
  void (*install_workflow)(uint32_t revision, uint16_t max_runs) = nullptr;   // runs = 0, saved
  uint32_t (*dropped_events)() = nullptr;
  void (*clear_dropped_events)() = nullptr;   // set to 0 and save the journal
  uint32_t (*next_sequence)() = nullptr;
  void (*acknowledge)(uint32_t sequence) = nullptr;
};

// Returns true when `command` (not null) was one of these routes and was consumed.
bool handle_command(char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_session
