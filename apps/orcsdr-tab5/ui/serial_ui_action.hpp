#pragma once
// RTL_UI ACTION <FM|AM|AIRBAND|CB|P25|LORA|SETTINGS> <action> [value]: drives a dashboard's own controls over serial,
// the way a tap would.
//
// A serial command family moved out of process_command() in main.cpp (see docs/SERIAL_COMMAND_MODULES.md). The two
// routes (the sign-in refusal, then the command), the action names, every range check and every reply are the ones
// the dispatcher had. The dashboards' action types come from their own headers; everything that acts on the device
// comes through Hooks, so this builds and is tested on the host and keeps no state.
#include <cstddef>
#include <cstdint>

#include "am_dashboard.hpp"
#include "cb_dashboard.hpp"
#include "fm_dashboard.hpp"
#include "lora_dashboard.hpp"
#include "p25_dashboard.hpp"
#include "settings_app.hpp"

namespace orcsdr::serial_ui_action {

// Every pointer must be set.
struct Hooks {
  void (*write_line)(const char* text) = nullptr;   // prints the text, then the line ending (Serial.println)
  bool (*airband_action)(const char* action, bool has_value, uint32_t value) = nullptr;   // airband::serial_action
  void (*fm_action)(const fm::Action& action) = nullptr;
  bool (*am_scan_prompt_active)() = nullptr;
  void (*am_discard_scan)() = nullptr;               // clear the AM scan results and refresh the screen
  void (*am_action)(const am::Action& action) = nullptr;
  bool (*cb_active)() = nullptr;                     // the receiver's band is CB
  void (*cb_action)(const cb::Action& action) = nullptr;
  void (*p25_action)(const p25::Action& action) = nullptr;
  size_t (*lora_region_count)() = nullptr;
  uint16_t (*lora_slot_count)() = nullptr;           // slots in the selected region
  void (*lora_action)(const lora::Action& action) = nullptr;
  uint8_t (*wifi_profile_count)() = nullptr;
  void (*settings_action)(const settings::Action& action) = nullptr;
};

// Returns true when `command` (not null) was one of these commands, whether it ran or was refused.
bool handle_command(const char* command, bool authenticated, const Hooks& hooks);

}  // namespace orcsdr::serial_ui_action
