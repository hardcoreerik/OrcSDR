#pragma once
// The RTL_WIFI serial commands: SCAN, CONNECT_SAVED, RESULTS, PROFILES, OUTAGE, DISCONNECT, the C6 power lab test
// (only in a build with ORCSDR_C6_FAULT_TEST), BUS_STATUS, STATUS, CHANNEL, C6_STATUS, C6_UPDATE and COEX_STATUS.
//
// A serial command family moved out of process_command() in main.cpp (see serial_help.hpp for the convention). The
// routes, their order, the authentication checks and every reply are the ones the dispatcher had. Everything the
// commands read from the device comes through Hooks, so the family builds and is tested on the host; it keeps no
// state of its own. The scan-result and profile listings keep their loops here and fetch one item per call.
#include <cstddef>
#include <cstdint>

namespace orcsdr::serial_wifi {

struct ScanSummary {   // the RTL_WIFI_RESULTS_BEGIN line
  unsigned count = 0;
  unsigned total = 0;
  unsigned revision = 0;
  unsigned age_s = 0;        // 0 when no scan has completed
  unsigned duration_ms = 0;
};

struct ScanResult {   // one access point; the pointers stay valid until the next hook call
  const char* ssid = "";
  const uint8_t* bssid = nullptr;   // 6 bytes
  int rssi = 0;
  unsigned channel = 0;
  int secure = 0;                   // 0 or 1
  const char* security = "";
  const char* phy = "";
  int ht40 = 0;                     // 0 or 1
};

struct BusStatus {
  int wlan_enable = 0;
  int wlan_input = 0;
  int hosted_ready = 0;
  int link_failed = 0;
  unsigned capture_state = 0;
};

struct Status {
  int station = 0;
  int hosted_match = 0;
  int scanning = 0;
  int connecting = 0;
  int connected = 0;
  unsigned saved_profiles = 0;
  int power = 0;
  int auto_connect = 0;
  const char* antenna = "internal";   // "external" or "internal"
  unsigned ap_count = 0;
  int rssi = 0;
  unsigned long outage_s = 0;
};

struct Link {   // RTL_WIFI_LINK, printed after STATUS only when the radio can report it
  int rssi = 0;
  unsigned channel = 0;
  unsigned second_channel = 0;
  int b = 0;
  int g = 0;
  int n = 0;
  int ax = 0;
  int ps_mode = 0;
  int bandwidth = 0;
};

struct Channel {
  bool ok = true;                    // false: the channel query failed
  const char* error = "";            // the error's name, when !ok
  unsigned primary = 0;
  unsigned secondary = 0;
  int connected = 0;
  bool has_ap = false;
  uint8_t ssid[33] = {};             // the connected access point's name, `ssid_length` bytes
  size_t ssid_length = 0;
  unsigned ap_primary = 0;
};

struct C6Status {
  const char* coprocessor = "";
  int transport = 0;
  int embedded = 0;
  const char* state = "";
  unsigned percent = 0;
  char stage[24] = {};               // empty when there is none
  int match = 0;
};

// Every pointer must be set, except `set_c6_power`, which only a build with ORCSDR_C6_FAULT_TEST needs.
struct Hooks {
  void (*write)(const char* text) = nullptr;        // prints the text as given, in one write
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  void (*write_hex)(const uint8_t* bytes, size_t size) = nullptr;   // lower-case hex, one character per write
  void (*request_scan)() = nullptr;
  uint8_t (*profile_count)() = nullptr;
  void (*connect_saved_profile)() = nullptr;        // selects profile 0 and queues the connect
  ScanSummary (*scan_summary)() = nullptr;
  uint8_t (*scan_result_count)() = nullptr;
  ScanResult (*scan_result)(uint8_t index) = nullptr;
  const char* (*profile_ssid)(uint8_t index) = nullptr;
  bool (*profile_connected)(uint8_t index) = nullptr;
  void (*simulate_outage)(uint32_t seconds) = nullptr;
  unsigned long (*outage_remaining_s)() = nullptr;
  bool (*disconnect)() = nullptr;                   // false: the radio would not pause
  bool (*set_c6_power)(bool enable) = nullptr;      // lab test: true when the write succeeded
  BusStatus (*bus_status)() = nullptr;              // also logs the radio power registers, before it reads
  Status (*status)() = nullptr;
  bool (*link)(Link* out) = nullptr;
  Channel (*channel)() = nullptr;
  C6Status (*c6_status)() = nullptr;
  bool (*begin_c6_update)() = nullptr;
  void (*print_coexistence)() = nullptr;            // prints the whole RTL_WIFI_COEX_STATUS line itself
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_wifi
