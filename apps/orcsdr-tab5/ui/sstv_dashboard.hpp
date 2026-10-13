#pragma once
// Native M5GFX SSTV dashboard (RECEIVE, IMAGES, SIGNAL, FREQUENCIES, SETUP).
// Drawing and touch hit-testing only: the dashboard never changes the receiver or touches the
// SD card. User choices come back as Actions that sstv_app applies.
#include <cstdint>
#include "sstv_receiver.hpp"
#include "sstv_settings.hpp"

namespace orcsdr::sstv {
enum class ActionKind : uint8_t {
  none,
  exit,
  tune_profile,     // value = profile index
  set_detection,    // value = sstv_settings::Detection (automatic or vis_only)
  select_mode,      // value = Mode ordinal; chooses manual detection for that mode
  save_image,       // value = gallery index, or -1 for the image in progress
  clear_gallery,
  toggle_autosave,
  abort_rx
};
struct Action { ActionKind kind = ActionKind::none; int32_t value = 0; };
struct UiSnapshot {
  bool receiver_ready = false;
  bool streaming = false;
  bool sd_ready = false;
  uint32_t frequency_hz = 0;
  float rf_dbfs = -90;
  sstv_settings::Settings settings{};
};
void ui_enter(const UiSnapshot& snapshot);
void ui_leave();
void ui_update(const UiSnapshot& snapshot);
bool ui_active();
Action ui_touch(int32_t x, int32_t y);
Page ui_page();
uint8_t ui_profile();
void ui_set_profile(uint8_t index);
void ui_set_settings(const sstv_settings::Settings& settings);   // redraws; nothing is restarted
void ui_notice(const char* message);
} // namespace orcsdr::sstv
