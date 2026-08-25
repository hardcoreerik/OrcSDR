#include "dashboard_audio_control.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace orcsdr::audio_header {
namespace {

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr int kRegionX = 866;
constexpr int kRegionY = 25;
constexpr int kRegionW = 174;
constexpr int kRegionH = 82;
constexpr int kIndicatorX = 870;
constexpr int kIndicatorW = 88;
constexpr int kButtonY = 34;
constexpr int kButtonH = 64;
constexpr int kButtonW = 54;
constexpr int kButtonX[] = {868, 926, 984};
constexpr int kHomeX = 1040;
constexpr int kHomeY = 8;
constexpr int kHomeW = 58;
constexpr int kHomeH = 58;
constexpr int kMuteX = 1099;
constexpr int kMuteY = 8;
constexpr int kMuteW = 58;
constexpr int kMuteH = 58;
constexpr int kVisualizerX = 1158;
constexpr int kVisualizerY = 8;
constexpr int kVisualizerW = 58;
constexpr int kVisualizerH = 58;
constexpr int kSettingsX = 1217;
constexpr int kSettingsY = 8;
constexpr int kSettingsW = 55;
constexpr int kSettingsH = 58;
constexpr uint32_t kTrayTimeoutMs = 4000;

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2) {
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void draw_speaker(int cx, int cy, uint16_t color, bool enabled) {
  M5.Display.fillTriangle(cx - 13, cy - 7, cx - 5, cy - 7, cx - 5, cy + 7, color);
  M5.Display.fillTriangle(cx - 5, cy - 7, cx + 5, cy - 14, cx + 5, cy + 14, color);
  if (enabled) {
    M5.Display.drawArc(cx + 4, cy, 12, 9, 300, 60, color);
    M5.Display.drawArc(cx + 4, cy, 19, 16, 300, 60, color);
  } else {
    M5.Display.drawLine(cx + 9, cy - 12, cx + 25, cy + 12, color);
    M5.Display.drawLine(cx + 25, cy - 12, cx + 9, cy + 12, color);
  }
}

void draw_battery(int x, int y, int32_t battery_percent) {
  M5.Display.drawRoundRect(x, y, 68, 32, 5, TFT_WHITE);
  M5.Display.fillRect(x + 68, y + 9, 6, 14, TFT_WHITE);
  const int pct = std::clamp<int32_t>(battery_percent, 0, 100);
  const int cells = battery_percent < 0 ? 0 : (pct + 24) / 25;
  for (int i = 0; i < 4; ++i)
    M5.Display.fillRect(x + 7 + i * 14, y + 6, 11, 20, i < cells ? kGreen : kGrid);
}

void draw_button(int x, const char* label, uint16_t color) {
  M5.Display.fillRoundRect(x, kButtonY, kButtonW, kButtonH, 9, kPanel);
  M5.Display.drawRoundRect(x, kButtonY, kButtonW, kButtonH, 9, color);
  text(label, x + kButtonW / 2, kButtonY + kButtonH / 2, color, 2);
}

}  // namespace

void reset(Control& control) { control = {}; }

void draw(const Control& control, uint8_t volume, bool sound_enabled,
          int32_t battery_percent) {
  M5.Display.fillRect(kRegionX, kRegionY, kRegionW, kRegionH, kBg);
  if (control.expanded) {
    draw_button(kButtonX[0], "-", kCyan);
    draw_button(kButtonX[1], sound_enabled ? "MUTE" : "UNMUTE",
                sound_enabled ? kGreen : kMuted);
    draw_button(kButtonX[2], "+", kCyan);
    return;
  }

  draw_speaker(884, 57, sound_enabled ? kGreen : kMuted, sound_enabled);
  char level[8];
  snprintf(level, sizeof(level), "%u", volume);
  text(level, 926, 67, sound_enabled ? TFT_WHITE : kMuted, 2);
  text("USB", 965, 67, TFT_WHITE, 1);
  draw_battery(966, 50, battery_percent);
}

void draw_home_button() {
  M5.Display.fillRoundRect(kHomeX, kHomeY, kHomeW, kHomeH, 8, kPanel);
  M5.Display.drawRoundRect(kHomeX, kHomeY, kHomeW, kHomeH, 8, kCyan);
  const int cx = kHomeX + kHomeW / 2;
  M5.Display.fillTriangle(cx, 17, kHomeX + 9, 38, kHomeX + kHomeW - 9, 38, kGreen);
  M5.Display.fillRect(kHomeX + 17, 35, 24, 21, kGreen);
  M5.Display.fillRect(cx - 4, 43, 8, 13, kPanel);
}

bool home_hit(int32_t x, int32_t y) {
  return hit(x, y, kHomeX, kHomeY, kHomeW, kHomeH);
}

void draw_mute_button(bool sound_enabled) {
  M5.Display.fillRoundRect(kMuteX, kMuteY, kMuteW, kMuteH, 8, kPanel);
  M5.Display.drawRoundRect(kMuteX, kMuteY, kMuteW, kMuteH, 8,
                           sound_enabled ? kGreen : kMuted);
  draw_speaker(kMuteX + 13, kMuteY + kMuteH / 2,
               sound_enabled ? kGreen : kMuted, sound_enabled);
}

bool mute_hit(int32_t x, int32_t y) {
  return hit(x, y, kMuteX, kMuteY, kMuteW, kMuteH);
}

void draw_visualizer_button(bool enabled) {
  const uint16_t color = enabled ? kCyan : kMuted;
  M5.Display.fillRoundRect(kVisualizerX, kVisualizerY, kVisualizerW, kVisualizerH, 8, kPanel);
  M5.Display.drawRoundRect(kVisualizerX, kVisualizerY, kVisualizerW, kVisualizerH, 8, color);
  text("VIS", kVisualizerX + kVisualizerW / 2, kVisualizerY + kVisualizerH / 2,
       color, 2);
}

bool visualizer_hit(int32_t x, int32_t y) {
  return hit(x, y, kVisualizerX, kVisualizerY, kVisualizerW, kVisualizerH);
}

void draw_settings_button() {
  constexpr int cx = kSettingsX + kSettingsW / 2;
  constexpr int cy = kSettingsY + kSettingsH / 2;
  M5.Display.fillRoundRect(kSettingsX, kSettingsY, kSettingsW, kSettingsH, 8, kPanel);
  M5.Display.drawRoundRect(kSettingsX, kSettingsY, kSettingsW, kSettingsH, 8, TFT_LIGHTGREY);
  M5.Display.drawCircle(cx, cy, 13, kCyan);
  M5.Display.drawCircle(cx, cy, 5, kCyan);
  M5.Display.drawLine(cx - 21, cy, cx - 13, cy, kCyan);
  M5.Display.drawLine(cx + 13, cy, cx + 21, cy, kCyan);
  M5.Display.drawLine(cx, cy - 21, cx, cy - 13, kCyan);
  M5.Display.drawLine(cx, cy + 13, cx, cy + 21, kCyan);
}

bool settings_hit(int32_t x, int32_t y) {
  return hit(x, y, kSettingsX, kSettingsY, kSettingsW, kSettingsH);
}

Action handle_touch(Control& control, int32_t x, int32_t y, uint32_t now_ms) {
  if (!control.expanded) {
    if (!hit(x, y, kIndicatorX, kRegionY, kIndicatorW, kRegionH)) return Action::none;
    control.expanded = true;
    control.hide_at_ms = now_ms + kTrayTimeoutMs;
    return Action::opened;
  }

  // Leave the Settings gear live while the tray is open.
  if (settings_hit(x, y)) return Action::none;
  for (size_t i = 0; i < 3; ++i) {
    if (!hit(x, y, kButtonX[i], kButtonY, kButtonW, kButtonH)) continue;
    control.hide_at_ms = now_ms + kTrayTimeoutMs;
    return i == 0 ? Action::volume_down
                  : i == 1 ? Action::sound_toggle : Action::volume_up;
  }
  control.expanded = false;
  control.hide_at_ms = 0;
  return Action::closed;
}

bool service_timeout(Control& control, uint32_t now_ms) {
  if (!control.expanded || static_cast<int32_t>(now_ms - control.hide_at_ms) < 0)
    return false;
  control.expanded = false;
  control.hide_at_ms = 0;
  return true;
}

bool self_check() {
  Control control{};
  if (handle_touch(control, 900, 60, 100) != Action::opened || !control.expanded)
    return false;
  if (handle_touch(control, kButtonX[0] + 1, kButtonY + 1, 200) != Action::volume_down)
    return false;
  if (handle_touch(control, kButtonX[1] + 1, kButtonY + 1, 300) != Action::sound_toggle)
    return false;
  if (handle_touch(control, kButtonX[2] + 1, kButtonY + 1, 400) != Action::volume_up)
    return false;
  if (handle_touch(control, kSettingsX + 1, kSettingsY + 1, 500) != Action::none)
    return false;
  if (service_timeout(control, 4399) || !service_timeout(control, 4400)) return false;
  reset(control);
  if (handle_touch(control, 800, 60, 0) != Action::none) return false;
  return kRegionX + kRegionW <= kHomeX && kButtonX[2] + kButtonW <= kHomeX &&
         kHomeX + kHomeW <= kMuteX && kMuteX + kMuteW <= kVisualizerX &&
         kVisualizerX + kVisualizerW <= kSettingsX &&
         home_hit(kHomeX + 1, kHomeY + 1) &&
         !home_hit(kHomeX - 1, kHomeY) &&
         mute_hit(kMuteX + 1, kMuteY + 1) && !mute_hit(kMuteX - 1, kMuteY) &&
         visualizer_hit(kVisualizerX + 1, kVisualizerY + 1) &&
         !visualizer_hit(kVisualizerX - 1, kVisualizerY) &&
         settings_hit(kSettingsX + 1, kSettingsY + 1) &&
         !settings_hit(kSettingsX - 1, kSettingsY);
}

}  // namespace orcsdr::audio_header
