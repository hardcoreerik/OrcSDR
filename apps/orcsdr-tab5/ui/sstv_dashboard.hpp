#pragma once
#include <cstdint>
#include "sstv_receiver.hpp"
namespace orcsdr::sstv {
enum class ActionKind : uint8_t { none, exit, tune_profile, select_mode, save_image, clear_gallery, toggle_autosave, abort_rx };
struct Action { ActionKind kind = ActionKind::none; int32_t value = 0; };
struct UiSnapshot {
  bool receiver_ready = false;
  bool streaming = false;
  bool sd_ready = false;
  bool autosave = true;
  uint32_t frequency_hz = 0;
  float rf_dbfs = -90;
};
void ui_enter(const UiSnapshot& snapshot);
void ui_leave();
void ui_update(const UiSnapshot& snapshot);
bool ui_active();
Action ui_touch(int32_t x, int32_t y);
Page ui_page();
uint8_t ui_profile();
bool ui_autosave();
void ui_set_autosave(bool enabled);
void ui_notice(const char* message);
} // namespace orcsdr::sstv
