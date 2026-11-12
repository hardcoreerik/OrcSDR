#include "focus_nav.hpp"
#include "dashboard_audio_control.hpp"

#include "orc_badge.hpp"
#include "map_download_notice.hpp"
#include "rtl_tcp_header_tag.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstddef>
#include <new>
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
constexpr int kHomeX = 1058;
constexpr int kHomeY = 8;
constexpr int kHomeW = 58;
constexpr int kHomeH = 58;
constexpr int kMuteX = 1138;
constexpr int kMuteY = 12;
constexpr int kMuteW = 54;
constexpr int kMuteH = 54;
// Settings takes the slot the VIS button had, so Home, speaker and Settings sit together.
constexpr int kSettingsX = 1212;
constexpr int kSettingsY = 12;
constexpr int kSettingsW = 54;
constexpr int kSettingsH = 54;
// Positions inside the tray, scaled from the 2000 x 667 artwork (840 / 2000 = 0.42).
constexpr int kIconX = kTrayX + 42, kIconY = kTrayY + 94, kIconW = 100, kIconH = 92;
constexpr int kLabelX = kTrayX + 176, kLabelY = kTrayY + 84;
constexpr int kSliderX = kTrayX + 176;
constexpr int kSliderY = kTrayY + 150;     // slider centre line
constexpr int kSliderW = 496;
constexpr int kTrackH = 24, kKnobR = 26;
constexpr int kPercentX = kTrayX + 766;
constexpr uint16_t kTrack = 0x2104;
constexpr uint32_t kTrayTimeoutMs = 5000;
uint16_t* g_tray_background = nullptr;
bool g_tray_background_valid = false;
M5Canvas* g_tray_sprite = nullptr;          // the open tray, composited over the dashboard
bool g_tray_sprite_valid = false;
uint8_t g_tray_sprite_volume = 0;
bool g_tray_sprite_enabled = true;
uint32_t g_tray_last_push_ms = 0;

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

// The speaker glyph at scale s (1 = header button, 2 = volume tray).
void draw_speaker(lgfx::LovyanGFX& g, int cx, int cy, uint16_t color, bool enabled, int s = 1) {
  g.fillTriangle(cx - 13 * s, cy - 7 * s, cx - 5 * s, cy - 7 * s, cx - 5 * s, cy + 7 * s, color);
  g.fillTriangle(cx - 5 * s, cy - 7 * s, cx + 5 * s, cy - 14 * s, cx + 5 * s, cy + 14 * s, color);
  if (enabled) {
    for (int t = 0; t < s; ++t) {
      g.drawArc(cx + 4 * s, cy, 12 * s + t, 9 * s + t, 300, 60, color);
      g.drawArc(cx + 4 * s, cy, 19 * s + t, 16 * s + t, 300, 60, color);
    }
  } else {
    for (int t = 0; t < s; ++t) {
      g.drawLine(cx + 9 * s + t, cy - 12 * s, cx + 25 * s + t, cy + 12 * s, color);
      g.drawLine(cx + 25 * s + t, cy - 12 * s, cx + 9 * s + t, cy + 12 * s, color);
    }
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

// Draws the tray into `g` with its top-left corner at (ox, oy): the display itself (kTrayX, kTrayY) or the off-screen sprite (0, 0).
void draw_volume_slider(lgfx::LovyanGFX& g, int ox, int oy, uint8_t volume, bool sound_enabled) {
  const uint16_t accent = sound_enabled ? kGreen : kMuted;
  const int dx = ox - kTrayX, dy = oy - kTrayY;   // the constants above are screen coordinates
  // Black body with a thick double cyan outline, like the artwork.
  g.fillRoundRect(kTrayX + dx, kTrayY + dy, kTrayW, kTrayH, 28, kBg);
  for (int i = 0; i < 4; ++i)
    g.drawRoundRect(kTrayX + i + dx, kTrayY + i + dy, kTrayW - 2 * i, kTrayH - 2 * i, 28 - i, kCyan);
  // Speaker in a green rounded square (also the mute button).
  for (int i = 0; i < 6; ++i)
    g.drawRoundRect(kIconX + i + dx, kIconY + i + dy, kIconW - 2 * i, kIconH - 2 * i, 20 - i, accent);
  draw_speaker(g, kIconX + 32 + dx, kIconY + kIconH / 2 + dy, accent, sound_enabled, 2);
  g.setTextDatum(middle_left);
  g.setTextSize(4);
  g.setTextColor(sound_enabled ? kCyan : kMuted, kBg);
  g.drawString(sound_enabled ? "VOLUME" : "MUTED", kLabelX + dx, kLabelY + dy);
  // Track, green fill and knob.
  g.fillRoundRect(kSliderX + dx, kSliderY - kTrackH / 2 + dy, kSliderW, kTrackH, kTrackH / 2, kTrack);
  const int fill = static_cast<int>((static_cast<uint32_t>(volume) * kSliderW) / 255u);
  if (fill > 0)
    g.fillRoundRect(kSliderX + dx, kSliderY - kTrackH / 2 + dy, fill, kTrackH, kTrackH / 2, accent);
  g.fillCircle(kSliderX + fill + dx, kSliderY + dy, kKnobR, accent);
  char value[8];
  snprintf(value, sizeof(value), "%u%%",
           static_cast<unsigned>((static_cast<uint16_t>(volume) * 100u + 127u) / 255u));
  g.setTextDatum(middle_center);
  g.setTextSize(4);
  g.setTextColor(TFT_WHITE, kBg);
  g.drawString(value, kPercentX + dx, kSliderY + dy);
}

void release_tray_sprite() {
  if (g_tray_sprite != nullptr) {
    g_tray_sprite->deleteSprite();
    delete g_tray_sprite;
    g_tray_sprite = nullptr;
  }
  g_tray_sprite_valid = false;
}

// Renders the tray into the sprite (when the volume or mute state changed) and pushes it onto the display.
void push_tray(uint8_t volume, bool sound_enabled, uint32_t now_ms) {
  if (g_tray_sprite == nullptr) {
    g_tray_sprite = new (std::nothrow) M5Canvas(&M5.Display);
    if (g_tray_sprite != nullptr) {
      g_tray_sprite->setColorDepth(16);
      g_tray_sprite->setPsram(true);
      if (!g_tray_sprite->createSprite(kTrayW, kTrayH)) release_tray_sprite();
      else g_tray_sprite_valid = false;      // a new sprite has never held the tray: render it even if the state is unchanged
    }
  }
  if (g_tray_sprite == nullptr) {         // no memory for the sprite: draw straight onto the display
    draw_volume_slider(M5.Display, kTrayX, kTrayY, volume, sound_enabled);
    g_tray_sprite_volume = volume;        // remember what is on screen so an unchanged tray is not repainted every loop;
    g_tray_sprite_enabled = sound_enabled;
    g_tray_sprite_valid = true;           // the allocation is retried at most once per kOverlayRepushMs
    g_tray_last_push_ms = now_ms;
    return;
  }
  if (!g_tray_sprite_valid || volume != g_tray_sprite_volume || sound_enabled != g_tray_sprite_enabled) {
    draw_volume_slider(*g_tray_sprite, 0, 0, volume, sound_enabled);
    g_tray_sprite_volume = volume;
    g_tray_sprite_enabled = sound_enabled;
    g_tray_sprite_valid = true;
  }
  g_tray_sprite->pushSprite(kTrayX, kTrayY);
  g_tray_last_push_ms = now_ms;
}

}  // namespace

void reset(Control& control) {
  control = {};
  g_tray_background_valid = false;
  release_tray_sprite();
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
  M5.Display.fillRect(20, 12, 298, 54, kBg);
  M5.Display.fillRect(20, 66, 350, 38, kBg);
  map_download_notice::invalidate();
  rtl_tcp_header_tag::invalidate();   // the header repaint wiped the RTL-TCP tag
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
  if (control.expanded) push_tray(volume, sound_enabled, millis());
}

void refresh_tray_overlay(const Control& control, uint8_t volume, bool sound_enabled, uint32_t now_ms) {
  if (!control.expanded) return;
  const bool changed = !g_tray_sprite_valid || volume != g_tray_sprite_volume || sound_enabled != g_tray_sprite_enabled;
  if (!changed && static_cast<int32_t>(now_ms - g_tray_last_push_ms) < static_cast<int32_t>(kOverlayRepushMs)) return;
  push_tray(volume, sound_enabled, now_ms);
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

bool header_button_hit(int32_t x, int32_t y) {
  return hit(x, y, kHomeX, kHomeY, kHomeW, kHomeH) ||
         hit(x, y, kSettingsX, kSettingsY, kSettingsW, kSettingsH);
}

bool tray_contains(int32_t x, int32_t y) { return hit(x, y, kTrayX, kTrayY, kTrayW, kTrayH); }
bool slider_hit(int32_t x, int32_t y) { return hit(x, y, kSliderX - 44, kSliderY - 60, kSliderW + 88, 120); }

void restore_volume_background() {
  release_tray_sprite();                  // the tray is closing: free its sprite
  if (!g_tray_background_valid) return;
  M5.Display.pushImage(kTrayX, kTrayY, kTrayW, kTrayH, g_tray_background);
  g_tray_background_valid = false;
}

void draw_home_button() {
  orcsdr::focus_nav::note(kHomeX, kHomeY, kHomeW, kHomeH);
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
  orcsdr::focus_nav::note(kMuteX, kMuteY, kMuteW, kMuteH);
  M5.Display.fillRoundRect(kMuteX, kMuteY, kMuteW, kMuteH, 8, kPanel);
  M5.Display.drawRoundRect(kMuteX, kMuteY, kMuteW, kMuteH, 8,
                           sound_enabled ? kGreen : kMuted);
  draw_speaker(M5.Display, kMuteX + 13, kMuteY + kMuteH / 2,
               sound_enabled ? kGreen : kMuted, sound_enabled);
}

bool mute_hit(int32_t x, int32_t y) {
  return hit(x, y, kMuteX, kMuteY, kMuteW, kMuteH);
}

void draw_visualizer_button(bool) {}      // removed: the visualizer is the last entry of All Dashboards

bool visualizer_hit(int32_t, int32_t) { return false; }

void draw_settings_button() {
  orcsdr::focus_nav::note(kSettingsX, kSettingsY, kSettingsW, kSettingsH);
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
    control.opened_ms = now_ms;
    return Action::opened;
  }

  if (slider_hit(x, y)) {   // generous target; the finger tracks along the bar
    control.volume = static_cast<uint8_t>(std::clamp<int32_t>(
        ((x - kSliderX) * 255 + kSliderW / 2) / kSliderW, 0, 255));
    control.hide_at_ms = now_ms + kTrayTimeoutMs;
    return Action::volume_set;
  }
  if (mute_hit(x, y) || hit(x, y, kIconX - 12, kIconY - 12, kIconW + 24, kIconH + 24)) {
    control.hide_at_ms = now_ms + kTrayTimeoutMs;
    // The second tap of a double tap on the speaker must not mute what the first tap just opened.
    if (static_cast<int32_t>(now_ms - control.opened_ms) < static_cast<int32_t>(kRepeatTapGuardMs))
      return Action::consumed;
    return Action::mute_toggle;
  }
  if (tray_contains(x, y)) {            // a blank spot inside the tray keeps it open and goes nowhere else
    control.hide_at_ms = now_ms + kTrayTimeoutMs;
    return Action::consumed;
  }
  // The Home, visualizer and Settings buttons stay live: the caller closes the tray and lets that tap through.
  if (header_button_hit(x, y)) return Action::none;
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
  // A second tap on the speaker straight after opening is swallowed; a later one toggles mute.
  if (handle_touch(control, kMuteX + 1, kMuteY + 1, 150, 255) != Action::consumed) return false;
  if (handle_touch(control, kMuteX + 1, kMuteY + 1, 600, 255) != Action::mute_toggle) return false;
  // Blank space inside the tray keeps it open; header buttons are left to the caller; outside closes it.
  if (handle_touch(control, kTrayX + 6, kTrayY + 6, 650, 255) != Action::consumed || !control.expanded) return false;
  if (handle_touch(control, kSettingsX + 1, kSettingsY + 1, 660, 255) != Action::none || !control.expanded) return false;
  if (service_timeout(control, 650 + kTrayTimeoutMs - 1) || !service_timeout(control, 650 + kTrayTimeoutMs)) return false;
  control.expanded = true;
  if (handle_touch(control, 40, 400, 700, 255) != Action::closed || control.expanded) return false;
  reset(control);
  if (handle_touch(control, 900, 60, 0, 128) != Action::none) return false;
  return kRegionX + kRegionW <= kHomeX && kTrayX >= 0 &&
         kTrayX + kTrayW <= 1280 && kTrayY > kMuteY + kMuteH &&
         kTrayY + kTrayH <= 720 && kTrayW == 3 * kTrayH &&
         kIconX + kIconW < kSliderX && kSliderX + kSliderW + kKnobR < kPercentX - 18 &&
         kHomeX + kHomeW <= kMuteX && kMuteX + kMuteW <= kSettingsX &&
         kSettingsX + kSettingsW <= 1280 &&
         home_hit(kHomeX + 1, kHomeY + 1) &&
         !home_hit(kHomeX - 1, kHomeY) &&
         mute_hit(kMuteX + 1, kMuteY + 1) && !mute_hit(kMuteX - 1, kMuteY) &&
         !visualizer_hit(kSettingsX + 1, kSettingsY + 1) &&
         settings_hit(kSettingsX + 1, kSettingsY + 1) &&
         !settings_hit(kSettingsX - 1, kSettingsY);
}

}  // namespace orcsdr::audio_header
