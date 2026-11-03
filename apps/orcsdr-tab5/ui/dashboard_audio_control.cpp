#include "focus_nav.hpp"
#include "dashboard_audio_control.hpp"

#include "orc_badge.hpp"
#include "map_download_notice.hpp"

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
HeaderLayout g_layout=make_header_layout({0,0,1280,720});
const int& kRegionX=g_layout.battery.x;
const int& kRegionY=g_layout.battery.y;
const int& kRegionW=g_layout.battery.w;
const int& kRegionH=g_layout.battery.h;
const int& kHomeX=g_layout.home.x;
const int& kHomeY=g_layout.home.y;
const int& kHomeW=g_layout.home.w;
const int& kHomeH=g_layout.home.h;
const int& kMuteX=g_layout.mute.x;
const int& kMuteY=g_layout.mute.y;
const int& kMuteW=g_layout.mute.w;
const int& kMuteH=g_layout.mute.h;
const int& kVisualizerX=g_layout.visualizer.x;
const int& kVisualizerY=g_layout.visualizer.y;
const int& kVisualizerW=g_layout.visualizer.w;
const int& kVisualizerH=g_layout.visualizer.h;
const int& kSettingsX=g_layout.settings.x;
const int& kSettingsY=g_layout.settings.y;
const int& kSettingsW=g_layout.settings.w;
const int& kSettingsH=g_layout.settings.h;
const int& kTrayX=g_layout.tray.x;
const int& kTrayY=g_layout.tray.y;
const int& kTrayW=g_layout.tray.w;
const int& kTrayH=g_layout.tray.h;
const int& kSliderX=g_layout.slider_x;
const int& kSliderY=g_layout.slider_y;
const int& kSliderW=g_layout.slider_w;
constexpr uint32_t kTrayTimeoutMs = 5000;
uint16_t* g_tray_background = nullptr;
size_t g_tray_background_capacity=0;
bool g_tray_background_valid = false;

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE,
          int size = 2) {
  M5.Display.setFont(nullptr);
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

bool set_viewport(HeaderRect viewport) {
  HeaderLayout next=make_header_layout(viewport);
  if (!next.valid) return false;
  g_layout=next;g_tray_background_valid=false;
  focus_nav::set_viewport(M5.Display.width(),M5.Display.height());
  return true;
}
const HeaderLayout& layout() { return g_layout; }

void reset(Control& control) {
  control = {};
  g_tray_background_valid = false;
}

void draw_badge() {
  const int x=g_layout.badge.x,y=g_layout.badge.y,size=g_layout.badge.w;
  M5.Display.fillRect(x, y, size, size, kBg);
  if (!badge::draw(x, y, size)) {
    M5.Display.drawRoundRect(x, y, size, size, 10, kGreen);
    text("O", x + size / 2, y + size / 2, kGreen, 4);
  }
}

void draw_brand(const char* subtitle) {
  if (g_layout.legacy) {
    M5.Display.fillRect(20,12,298,54,kBg);M5.Display.fillRect(20,66,350,38,kBg);
  } else {
    const auto& b=g_layout.brand;M5.Display.fillRect(b.x,b.y,b.w,b.h,kBg);
  }
  map_download_notice::invalidate();
  draw_badge();
  M5.Display.setFont(nullptr);M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(TFT_WHITE,kBg);M5.Display.setTextSize(g_layout.brand_size);
  M5.Display.drawString("OrcSDR",g_layout.brand_x,g_layout.brand_y);
  M5.Display.setTextColor(kCyan,kBg);
  const int subtitle_size=g_layout.legacy && subtitle && std::strlen(subtitle)<=10?3:2;
  M5.Display.setTextSize(subtitle_size);
  char visible[48];
  snprintf(visible,sizeof(visible),"%s",subtitle?subtitle:"");
  if (!g_layout.legacy) {
    const int available=g_layout.brand.x+g_layout.brand.w-g_layout.brand_x;
    while (std::strlen(visible)>3 && M5.Display.textWidth(visible)>available) {
      const size_t n=std::strlen(visible);
      if (n>=3 && !std::strcmp(visible+n-3,"...")) {
        visible[n-4]='.';visible[n-1]='\0';
      } else {
        visible[n-3]='.';visible[n-2]='.';visible[n-1]='.';
      }
    }
  }
  M5.Display.drawString(visible,g_layout.brand_x,g_layout.subtitle_y);
}

void draw_battery(int32_t battery_percent) {
  M5.Display.fillRect(kRegionX, kRegionY, kRegionW, kRegionH, kBg);
  char level[8];
  if (battery_percent < 0)
    snprintf(level, sizeof(level), "--");
  else
    snprintf(level, sizeof(level), "%ld%%",
             static_cast<long>(std::clamp<int32_t>(battery_percent, 0, 100)));
  M5.Display.setFont(nullptr);
  M5.Display.setTextSize(2);
  // The battery icon supplies context when the percent suffix cannot fit.
  if (battery_percent>=0 && M5.Display.textWidth(level)>kRegionW)
    level[std::strlen(level)-1]='\0';
  // Compact noninteractive indication occupies the former VIS slot.
  const int x=kRegionX+4,y=kRegionY+3,w=kRegionW-10,h=18;
  M5.Display.drawRoundRect(x,y,w,h,3,TFT_WHITE);
  M5.Display.fillRect(x+w,y+5,3,8,TFT_WHITE);
  if (battery_percent>=0) {
    const int fill=(w-6)*std::clamp<int32_t>(battery_percent,0,100)/100;
    if (fill>0) M5.Display.fillRect(x+3,y+3,fill,h-6,kGreen);
  }
  text(level,kRegionX+kRegionW/2,kRegionY+38,battery_percent<0?kMuted:TFT_WHITE,2);
}

void draw(const Control& control, uint8_t volume, bool sound_enabled,
          int32_t battery_percent) {
  M5.Display.fillRect(kRegionX, kRegionY, kRegionW, kRegionH, kBg);
  draw_battery(battery_percent);
  if (control.expanded) draw_volume_slider(volume, sound_enabled);
}

void capture_volume_background() {
  const size_t wanted=size_t(kTrayW)*kTrayH*sizeof(uint16_t);
  if (wanted>g_tray_background_capacity) {
    heap_caps_free(g_tray_background);
    g_tray_background=static_cast<uint16_t*>(heap_caps_malloc(wanted,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    g_tray_background_capacity=g_tray_background?wanted:0;
    g_tray_background_valid=false;
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
  orcsdr::focus_nav::note(kHomeX, kHomeY, kHomeW, kHomeH);
  M5.Display.fillRoundRect(kHomeX, kHomeY, kHomeW, kHomeH, 8, kPanel);
  M5.Display.drawRoundRect(kHomeX, kHomeY, kHomeW, kHomeH, 8, kCyan);
  const int cx = kHomeX + kHomeW / 2;
  if (g_layout.legacy) {
    M5.Display.fillTriangle(cx,17,kHomeX+9,38,kHomeX+kHomeW-9,38,kGreen);
    M5.Display.fillRect(kHomeX+17,35,24,21,kGreen);
    M5.Display.fillRect(cx-4,43,8,13,kPanel);
  } else {
    const int left=kHomeX+(kHomeW-32)/2,top=kHomeY+(kHomeH-36)/2;
    M5.Display.fillTriangle(cx,top,left,top+20,left+32,top+20,kGreen);
    M5.Display.fillRect(left+6,top+18,20,18,kGreen);
    M5.Display.fillRect(cx-3,top+25,6,11,kPanel);
  }
}

bool home_hit(int32_t x, int32_t y) {
  return hit(x, y, kHomeX, kHomeY, kHomeW, kHomeH);
}

void draw_mute_button(bool sound_enabled) {
  orcsdr::focus_nav::note(kMuteX, kMuteY, kMuteW, kMuteH);
  M5.Display.fillRoundRect(kMuteX, kMuteY, kMuteW, kMuteH, 8, kPanel);
  M5.Display.drawRoundRect(kMuteX, kMuteY, kMuteW, kMuteH, 8,
                           sound_enabled ? kGreen : kMuted);
  draw_speaker(kMuteX + 13, kMuteY + kMuteH / 2,
               sound_enabled ? kGreen : kMuted, sound_enabled);
}

bool mute_hit(int32_t x, int32_t y) {
  return hit(x, y, kMuteX, kMuteY, kMuteW, kMuteH);
}

// Compatibility hooks: existing callers remain valid, but VIS is neither
// drawn nor a touch/focus target. Visualizer screens remain available in source.
void draw_visualizer_button(bool enabled) { (void)enabled; }
bool visualizer_hit(int32_t x,int32_t y) { (void)x;(void)y;return false; }

void draw_settings_button() {
  orcsdr::focus_nav::note(kSettingsX, kSettingsY, kSettingsW, kSettingsH);
  const int cx = kSettingsX + kSettingsW / 2;
  const int cy = kSettingsY + kSettingsH / 2;
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

  const int touch_height=g_layout.legacy?40:44;
  if (hit(x, y, kSliderX - 12, kSliderY - touch_height/2, kSliderW + 24, touch_height)) {
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
  if (handle_touch(control, -1, -1, 0, 128) != Action::none) return false;
  return g_layout.valid && header_inside(g_layout.viewport,g_layout.tray) &&
         !header_overlap(g_layout.home,g_layout.mute) &&
         !header_overlap(g_layout.mute,g_layout.visualizer) &&
         !header_overlap(g_layout.visualizer,g_layout.settings) &&
         home_hit(kHomeX+1,kHomeY+1) && !home_hit(kHomeX-1,kHomeY) &&
         mute_hit(kMuteX+1,kMuteY+1) && !mute_hit(kMuteX-1,kMuteY) &&
         !visualizer_hit(kRegionX+1,kRegionY+1) &&
         settings_hit(kSettingsX+1,kSettingsY+1) && !settings_hit(kSettingsX-1,kSettingsY);
}

}  // namespace orcsdr::audio_header
