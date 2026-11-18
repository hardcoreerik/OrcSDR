#pragma once
// The rules that keep Wi-Fi up, moved out of poll_wifi() in main.cpp: handling a lost C6 link once, earning the recovery
// budget back after 5 minutes up, the minute health line, the opt-in keep-alive for a remote receiver, and the timed
// reconnect. Scans, connects and the status polling stay in main.cpp. No device headers: the Wi-Fi variables are main.cpp's,
// reached through pointers, and every device call goes through Hooks.
#include "action_io.hpp"

#include <cstdint>

namespace orcsdr::wifi_supervisor {

constexpr uint32_t kBudgetRefillMs = 300000;   // a link up this long earns back its recovery budget
constexpr uint32_t kHealthMs = 60000;
constexpr uint32_t kKeepaliveDownMs = 300000;  // down this long, and this long since the last re-arm
constexpr uint32_t kLostRecoveryDelayMs = 2000;

struct State {
  bool link_failure_handled = false;
  uint32_t down_since_ms = 0;
  uint32_t last_rearm_ms = 0;
  uint32_t last_health_ms = 0;
};

// main.cpp's Wi-Fi variables. Every pointer must be set.
struct Vars {
  bool* station_ready = nullptr;
  bool* connected = nullptr;
  bool* connecting = nullptr;
  bool* scan_running = nullptr;
  bool* auto_reconnect_armed = nullptr;
  bool* save_after_connect = nullptr;
  uint8_t* link_recoveries = nullptr;
  uint8_t* reconnect_attempts = nullptr;
  uint32_t* link_recovery_due_ms = nullptr;
  uint32_t* reconnect_due_ms = nullptr;
  uint32_t* connected_since_ms = nullptr;
  uint32_t* scan_revision = nullptr;
  uint8_t link_recovery_max = 0;   // kWifiLinkRecoveryMax
};

struct Hooks {
  action_io::Io io;
  uint32_t (*now_ms)() = nullptr;
  bool (*power_enabled)() = nullptr;            // settings_wifi_power_enabled
  void (*clear_requests)() = nullptr;           // drop the queued scan and connect requests
  bool (*link_failed)() = nullptr;
  void (*resume_radio_connect_pause)() = nullptr;
  void (*set_status)(const char* text) = nullptr;
  void (*draw_state)() = nullptr;
  void (*recover_link)() = nullptr;
  bool (*remote_may_need_wifi)() = nullptr;
  uint32_t (*dma_free_bytes)() = nullptr;
  const char* (*stream_state_name)() = nullptr;
  bool (*keepalive_enabled)() = nullptr;
  void (*schedule_reconnect)(const char* why) = nullptr;
  void (*resume_deferred_radio)() = nullptr;
  bool (*profile_ssid_empty)() = nullptr;       // !wifi_ssid[0]
  uint8_t (*profile_count)() = nullptr;
  void (*select_first_profile)() = nullptr;
  void (*request_connect)() = nullptr;          // wifi_connect_requested = true
};

// Runs the supervision rules once. Returns false where the old code returned (Wi-Fi off, link lost or recovery pending);
// true when the caller should go on with scan and connect requests and the status polling.
bool service(State& state, const Vars& vars, const Hooks& hooks);

}  // namespace orcsdr::wifi_supervisor
