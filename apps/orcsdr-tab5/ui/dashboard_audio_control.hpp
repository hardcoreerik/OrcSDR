#pragma once

#include <cstdint>

namespace orcsdr::audio_header {

// The header's status-bar area, between the brand and the battery/buttons. Home draws the Wi-Fi /
// RTL-SDR / clock bar here; every other dashboard leaves it empty (a design constraint so the shared
// status bar can appear on any screen without colliding with dashboard content).
// x318..412, y12..66 is reserved for the shared map-download status/shortcut.
constexpr int kTitleDividerX = 420;
constexpr int kTitleTextX = 442;
constexpr int kStatusBarX = 420;
constexpr int kStatusBarY = 12;
constexpr int kStatusBarW = 446;
constexpr int kStatusBarH = 54;

enum class Action : uint8_t {
  none,         // not for the tray: the caller may treat the touch as its own
  opened,
  closed,
  volume_set,
  mute_toggle,
  consumed,     // belongs to the tray (a blank spot inside it, or a repeat tap) but changes nothing
};

struct Control {
  bool expanded = false;
  uint8_t volume = 0;
  uint32_t hide_at_ms = 0;
  uint32_t opened_ms = 0;   // a second tap on the speaker within kRepeatTapGuardMs of opening is ignored
};

// Tray geometry: a 3:1 popup centred under the status bar, drawn from the volume-control artwork.
constexpr int kTrayW = 840;      // twice the first design; the artwork's 3:1 shape is kept
constexpr int kTrayH = 280;
constexpr int kTrayX = kStatusBarX + (kStatusBarW - kTrayW) / 2;
constexpr int kTrayY = kStatusBarY + kStatusBarH + 6;
constexpr uint32_t kRepeatTapGuardMs = 400;
// True for the Home and Settings buttons (they stay usable while the tray is open).
bool header_button_hit(int32_t x, int32_t y);
bool tray_contains(int32_t x, int32_t y);
bool slider_hit(int32_t x, int32_t y);   // the draggable slider zone: the only part of the tray that reacts to a held finger

void reset(Control& control);
void draw_badge();
void draw_brand(const char* subtitle);
void draw_battery(int32_t battery_percent);
void draw(const Control& control, uint8_t volume, bool sound_enabled,
          int32_t battery_percent);
// Keeps the open tray the top drawn item. It is rendered once into an off-screen sprite and pushed onto the display over whatever
// a dashboard painted there. Call it every loop; it re-pushes at most every kOverlayRepushMs (at once when the volume or mute
// state changed) and does nothing while the tray is closed. Dashboards also stop repainting under an open tray.
constexpr uint32_t kOverlayRepushMs = 1000;   // safety net only: dashboards hold still while the tray is open
void refresh_tray_overlay(const Control& control, uint8_t volume, bool sound_enabled, uint32_t now_ms);
void capture_volume_background();
void restore_volume_background();
void draw_home_button();
bool home_hit(int32_t x, int32_t y);
void draw_mute_button(bool sound_enabled);
bool mute_hit(int32_t x, int32_t y);
// The header no longer has a VIS button (the visualizer is the last entry of All Dashboards). These stay so every dashboard
// keeps compiling; draw nothing and never hit.
void draw_visualizer_button(bool enabled);
bool visualizer_hit(int32_t x, int32_t y);
void draw_settings_button();
bool settings_hit(int32_t x, int32_t y);
Action handle_touch(Control& control, int32_t x, int32_t y, uint32_t now_ms,
                    uint8_t current_volume);
bool service_timeout(Control& control, uint32_t now_ms);
bool self_check();

}  // namespace orcsdr::audio_header
