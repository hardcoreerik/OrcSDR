#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::wifi {

struct ScanResult {
  char ssid[33]{};
  uint8_t bssid[6]{};
  int16_t rssi = 0;
  uint8_t channel = 0;
  int8_t secondary_channel_offset = 0;
  char security[24]{};
  char phy[12]{};
  bool secure = false;
};

enum class C6UpdateState : uint8_t { unavailable, unreachable, current, optional, ready, updating, failed, rebooting };

struct C6UpdateStatus {
  C6UpdateState state = C6UpdateState::unavailable;
  uint8_t progress_percent = 0;
  bool image_embedded = false;
  char stage[24]{};
};

bool start();
void apply_max_tx_power();                 // re-applies the cap below (called on every start)
// C6 transmit cap, 0.25 dBm units (8..84); 0 = full power. apply_now=false only stores it: the next start() applies it
// (use that while the link is down, where a command to the C6 would only time out).
void set_max_tx_power_qdbm(int8_t cap, bool apply_now = true);
uint32_t c6_init_count();                  // C6 boots announced over ESP-Hosted (CP_INIT events) since P4 boot
int32_t c6_last_reset_reason();            // esp_reset_reason_t of the latest C6 boot, -1 before the first
void stop();
bool begin_scan();
int scan_results(ScanResult* results, size_t capacity);
bool connect(const char* ssid, const char* password);
void disconnect();
bool connected();
// Test fault injection (authenticated serial `RTL_WIFI_OUTAGE <seconds>`, 0 cancels): behaves like an access point that vanished
// for that long. A live link is dropped (a real disassociation), and every connect request fails as if the network were not
// found (the C6 is asked to join a network that does not exist), until the time is up; then the next real connect works. Used to test reconnect, give-up and recovery repeatably
// without switching a router off. Nothing is persisted and it never outlives a reset.
void simulate_outage(uint32_t seconds);
uint32_t outage_remaining_s();
// True once the SDIO link to the C6 has failed; Wi-Fi stays down until restart.
bool link_failed();
// A transport-level start() failure (Hosted init, connect or version query) is treated like a lost link,
// so the bounded recovery runs instead of leaving Wi-Fi blocked until the next reboot.
void note_start_failure();
// Tear down a failed Hosted link so start() can bring it up again (the caller
// power-cycles the C6 in between); end_link_recovery() re-enables failure
// detection once start() has returned.
bool begin_link_recovery();
// True when an ESP-Hosted teardown timed out and is still blocked: Wi-Fi cannot be re-initialised until the Tab5
// restarts. begin_link_recovery() then returns false within its wait instead of freezing the caller.
bool link_wedged();
void end_link_recovery();
bool connect_failed();
const char* ssid();
const char* ip();
int16_t rssi();
// Live radio link facts for diagnostics (one RPC to the C6). Returns false if the query failed.
struct LinkDetails { int8_t rssi; uint8_t channel; uint8_t second_channel; bool b, g, n, ax; int ps_mode; int bandwidth; };
bool link_details(LinkDetails* out);
bool hosted_versions_match();  // C6 firmware is on the tested list (2.12.6 or 3.0.6)
const char* hosted_c6_version();
bool hosted_transport_ready();
const char* hosted_failure_stage();
int32_t hosted_failure_code();
C6UpdateStatus c6_update_status();
const char* c6_update_state_name(C6UpdateState state);
bool begin_c6_update(bool allow_current_for_orcdial = false);

}  // namespace orcsdr::wifi
