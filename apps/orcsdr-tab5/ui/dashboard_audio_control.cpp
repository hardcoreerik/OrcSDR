#include "dashboard_audio_control.hpp"

#include "orc_badge.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace orcsdr::audio_header {
namespace {

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0841;
constexpr uint16_t kCyan = 0x2e7f;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kGrid = 0x2945;
constexpr int kRegionX = 866;
constexpr int kRegionY = 12;
constexpr int kRegionW = 174;
constexpr int kRegionH = 54;
constexpr int kHomeX = 1040;
constexpr int kHomeY = 8;
constexpr int kHomeW = 58;
constexpr int kHomeH = 58;
constexpr int kMuteX = 1099;
constexpr int kMuteY = 12;
constexpr int kMuteW = 54;
constexpr int kMuteH = 54;
constexpr int kVisualizerX = 1158;
constexpr int kVisualizerY = 12;
constexpr int kVisualizerW = 54;
constexpr int kVisualizerH = 54;
constexpr int kSettingsX = 1217;
constexpr int kSettingsY = 12;
constexpr int kSettingsW = 51;
constexpr int kSettingsH = 54;
constexpr int kTrayX = 900;
constexpr int kTrayY = 72;
constexpr int kTrayW = 280;
constexpr int kTrayH = 68;
constexpr int kSliderX = 924;
constexpr int kSliderY = 113;
constexpr int kSliderW = 232;
constexpr uint32_t kTrayTimeoutMs = 5000;
uint16_t* g_tray_background = nullptr;
bool g_tray_background_valid = false;

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

void draw_volume_slider(uint8_t volume, bool sound_enabled) {
  M5.Display.fillRoundRect(kTrayX, kTrayY, kTrayW, kTrayH, 9, kPanel);
  M5.Display.drawRoundRect(kTrayX, kTrayY, kTrayW, kTrayH, 9, kCyan);
  text(sound_enabled ? "VOLUME" : "MUTED", kTrayX + 51, kTrayY + 20,
       sound_enabled ? kCyan : kMuted, 1);
  char value[8];
  snprintf(value, sizeof(value), "%u%%",
           static_cast<unsigned>((static_cast<uint16_t>(volume) * 100u + 127u) / 255u));
  text(value, kTrayX + kTrayW - 36, kTrayY + 20, TFT_WHITE, 1);
  M5.Display.fillRoundRect(kSliderX, kSliderY - 4, kSliderW, 8, 4, kGrid);
  const int fill = static_cast<int>((static_cast<uint32_t>(volume) * kSliderW) / 255u);
  if (fill > 0)
    M5.Display.fillRoundRect(kSliderX, kSliderY - 4, fill, 8, 4,
                             sound_enabled ? kGreen : kMuted);
  M5.Display.fillCircle(kSliderX + fill, kSliderY, 9,
                        sound_enabled ? kGreen : kMuted);
}

}  // namespace

void reset(Control& control) {
  control = {};
  g_tray_background_valid = false;
}

void draw_badge() {
  constexpr int x = 24;
  constexpr int y = 14;
  constexpr int size = 88;
  M5.Display.fillRect(x, y, size, size, kBg);
  if (!badge::draw(x, y, size)) {
    M5.Display.drawRoundRect(x, y, size, size, 10, kGreen);
    text("O", x + size / 2, y + size / 2, kGreen, 4);
  }
}

void draw_brand(const char* subtitle) {
  M5.Display.fillRect(20, 12, 350, 92, kBg);
  draw_badge();
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(TFT_WHITE, kBg);
  M5.Display.setTextSize(5);
  M5.Display.drawString("OrcSDR", 128, 38);
  M5.Display.setTextColor(kCyan, kBg);
  M5.Display.setTextSize(subtitle && std::strlen(subtitle) <= 10 ? 3 : 2);
  M5.Display.drawString(subtitle ? subtitle : "", 128, 82);
}

void draw_battery(int32_t battery_percent) {
  M5.Display.fillRect(kRegionX, kRegionY, kRegionW, kRegionH, kBg);
  char level[8];
  if (battery_percent < 0)
    snprintf(level, sizeof(level), "--");
  else
    snprintf(level, sizeof(level), "%ld%%",
             static_cast<long>(std::clamp<int32_t>(battery_percent, 0, 100)));
  text(level, 910, 37, battery_percent < 0 ? kMuted : TFT_WHITE, 2);
  draw_battery(950, 21, battery_percent);
}

void draw(const Control& control, uint8_t volume, bool sound_enabled,
          int32_t battery_percent) {
  M5.Display.fillRect(kRegionX, kRegionY, kRegionW, kRegionH, kBg);
  draw_battery(battery_percent);
  if (control.expanded) draw_volume_slider(volume, sound_enabled);
}

void capture_volume_background() {
  if (g_tray_background == nullptr) {
    g_tray_background = static_cast<uint16_t*>(heap_caps_malloc(
        kTrayW * kTrayH * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (g_tray_background == nullptr) return;
  M5.Display.readRect(kTrayX, kTrayY, kTrayW, kTrayH, g_tray_background);
  g_tray_background_valid = true;
}

void restore_volume_background() {
  if (!g_tray_background_valid) return;
  M5.Display.pushImage(kTrayX, kTrayY, kTrayW, kTrayH, g_tray_background);
  g_tray_background_valid = false;
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

Action handle_touch(Control& control, int32_t x, int32_t y, uint32_t now_ms,
                    uint8_t current_volume) {
  if (!control.expanded) {
    if (!mute_hit(x, y)) return Action::none;
    control.expanded = true;
    control.volume = current_volume;
    control.hide_at_ms = now_ms + kTrayTimeoutMs;
    return Action::opened;
  }

  if (hit(x, y, kSliderX - 12, kSliderY - 20, kSliderW + 24, 40)) {
    control.volume = static_cast<uint8_t>(std::clamp<int32_t>(
        ((x - kSliderX) * 255 + kSliderW / 2) / kSliderW, 0, 255));
    control.hide_at_ms = now_ms + kTrayTimeoutMs;
    return Action::volume_set;
  }
  if (mute_hit(x, y)) {
    control.hide_at_ms = now_ms + kTrayTimeoutMs;
    return Action::mute_toggle;
  }
  // Leave the Settings gear live while the tray is open.
  if (settings_hit(x, y)) return Action::none;
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
  if (handle_touch(control, kMuteX + 1, kMuteY + 1, 100, 128) != Action::opened ||
      !control.expanded || control.volume != 128)
    return false;
  if (handle_touch(control, kSliderX, kSliderY, 200, 128) != Action::volume_set ||
      control.volume != 0)
    return false;
  if (handle_touch(control, kSliderX + kSliderW, kSliderY, 300, 0) !=
          Action::volume_set ||
      control.volume != 255)
    return false;
  if (handle_touch(control, kMuteX + 1, kMuteY + 1, 400, 255) !=
      Action::mute_toggle)
    return false;
  if (service_timeout(control, 5399) || !service_timeout(control, 5400)) return false;
  reset(control);
  if (handle_touch(control, 900, 60, 0, 128) != Action::none) return false;
  return kRegionX + kRegionW <= kHomeX && kTrayX >= 0 &&
         kTrayX + kTrayW <= 1280 && kTrayY > kMuteY + kMuteH &&
         kTrayY + kTrayH <= 720 &&
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
