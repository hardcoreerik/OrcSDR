#pragma once
// Adaptive transmit-power cap for the Tab5's C6 Wi-Fi co-processor. No device headers, no globals: host-testable.
//
// On a disturbed USB supply (a powered hub with other devices attached) the C6 browns out or power-on resets during its
// transmit bursts; its own log shows "Brownout detector was triggered" or rst:0x1 (POWERON). Transmit current is the one
// part of that the firmware controls, so after each C6 restart that a recovery attributes to the supply, the cap steps
// down (full power -> 15 dBm -> 11 dBm). After the link has stayed up for kStepUpMs it steps back up one level, so a unit
// on a clean supply keeps full range and one on bad power trades some range for staying connected.
#include <cstdint>

namespace orcsdr::c6_tx_policy {

constexpr uint32_t kStepUpMs = 600000;     // 10 minutes of stable link before one step back up
constexpr uint32_t kStepUpMaxMs = 7200000; // the wait doubles after each failed step up, to 2 hours at most
constexpr uint8_t kLevels = 3;
// Cap per level, in the C6's 0.25 dBm units; 0 = no cap (full power).
constexpr int8_t kCapQdbm[kLevels] = {0, 60, 44};

// esp_reset_reason_t values this policy reads (kept here so the module needs no ESP-IDF header).
constexpr int32_t kResetPowerOn = 1;       // ESP_RST_POWERON
constexpr int32_t kResetBrownout = 9;      // ESP_RST_BROWNOUT
constexpr int32_t kResetPowerGlitch = 14;  // ESP_RST_PWR_GLITCH

struct State {
  uint8_t level = 0;
  uint32_t stable_since_ms = 0;            // link up and no supply restart since this time; 0 = not counting
  uint32_t supply_restarts = 0;            // for the status line
  uint32_t step_up_ms = kStepUpMs;         // current stable time needed before a step up (backs off on failed step ups)
  uint32_t last_step_up_ms = 0;            // when the last step up happened; 0 = none since the last failure
};

// The C6 link failed (first failure of a recovery series). On a clean supply this essentially never happens; on a
// disturbed one it is the C6 browning out or power-on resetting during a transmit burst. The C6 firmware in use reports
// its reset reason as 0 (unknown) in its INIT event, so the link failure itself is the trigger. Returns true when the
// cap changed (one step down, to 11 dBm at most).
bool on_link_failure(State& s, uint32_t now_ms);

// Same step, but only when the C6 does report a supply reset reason (brown-out, power-on, power glitch). Kept for a C6
// firmware that fills the field in.
bool on_c6_restart(State& s, int32_t reset_reason, uint32_t now_ms);

// Called regularly. link_up = Wi-Fi connected. Returns true when the cap changed (one step up after kStepUpMs stable).
bool service(State& s, bool link_up, uint32_t now_ms);

inline int8_t cap_qdbm(const State& s) { return kCapQdbm[s.level < kLevels ? s.level : kLevels - 1]; }

}  // namespace orcsdr::c6_tx_policy
