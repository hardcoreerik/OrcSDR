#include "freq_keypad.hpp"

#include <M5Unified.h>

#include "focus_nav.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::freq_keypad {
namespace {

constexpr uint16_t kCyan = 0x05FF;
constexpr uint16_t kGreen = 0x6FE0;
constexpr uint16_t kMuted = 0x8c71;
constexpr uint16_t kKeyFill = 0x2945;
constexpr uint16_t kCardFill = 0x0841;

Layout g_layout=make_layout({0,0,1280,720});
constexpr char kKeys[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0', '\b'};

char g_hint[24]{};
char g_unit[8]{};

void label(const char* value, int x, int y, uint16_t color, uint16_t background,
           uint8_t size, int max_width = 0) {
  M5.Display.setFont(nullptr);
  while (size>2) {
    M5.Display.setTextSize(size);
    if (!max_width || M5.Display.textWidth(value)<=max_width) break;
    --size;
  }
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color, background);
  M5.Display.setTextSize(size);
  char visible[64];
  snprintf(visible,sizeof(visible),"%s",value);
  const char* shown=visible;
  while (max_width>0 && shown[0] && M5.Display.textWidth(shown)>max_width) ++shown;
  M5.Display.drawString(shown, x, y);
}

void key(int x, int y, int w, int h, const char* value, uint16_t edge, uint16_t fill,
         uint16_t color, uint8_t size) {
  M5.Display.fillRoundRect(x, y, w, h, 10, fill);
  M5.Display.drawRoundRect(x, y, w, h, 10, edge);
  label(value, x + w / 2, y + h / 2, color, fill, size, w-12);
}

void draw_field(const char* entry) {
  const auto& f=g_layout.field;
  M5.Display.fillRoundRect(f.x, f.y, f.w, f.h, 10, TFT_NAVY);
  char field[40];
  if (entry[0]) snprintf(field, sizeof(field), "%s %s", entry, g_unit);
  else snprintf(field, sizeof(field), "%s", g_hint);
  label(field, f.x + f.w / 2, f.y + f.h / 2,
        entry[0] ? TFT_WHITE : kMuted, TFT_NAVY, entry[0] ? g_layout.entry_size : g_layout.hint_size, f.w-12);
}

}  // namespace

void draw(int clear_top, uint16_t background, const char* title, const char* hint,
          const char* unit, const char* entry, const Rect* viewport) {
  snprintf(g_hint, sizeof(g_hint), "%s", hint ? hint : "");
  snprintf(g_unit, sizeof(g_unit), "%s", unit ? unit : "");
  const int width=M5.Display.width(),height=M5.Display.height();
  focus_nav::set_viewport(width,height);
  Rect v=viewport ? *viewport : Rect{0,0,width,height};
  const int right=std::min(width,v.x+v.w),bottom=std::min(height,v.y+v.h);
  v.x=std::max(0,v.x);v.y=std::max(std::max(0,clear_top),v.y);
  v.w=std::max(0,right-v.x);v.h=std::max(0,bottom-v.y);
  g_layout=make_layout(v);
  M5.Display.setFont(nullptr);
  M5.Display.clearScrollRect();
  M5.Display.fillRect(v.x,v.y,v.w,v.h,background);
  if (!g_layout.valid) return; // caller retains navigation; never install off-screen hit regions
  const auto& c=g_layout.card;const auto& t=g_layout.title;
  M5.Display.fillRoundRect(c.x,c.y,c.w,c.h,16,kCardFill);
  M5.Display.drawRoundRect(c.x,c.y,c.w,c.h,16,kCyan);
  label(title ? title : "ENTER FREQUENCY",t.x+t.w/2,t.y+t.h/2,kCyan,kCardFill,3,t.w-12);
  draw_field(entry ? entry : "");
  for (int i=0;i<12;++i) {
    const char value[2]={kKeys[i]=='\b' ? '<' : kKeys[i],'\0'};
    const auto& r=g_layout.keys[i];
    key(r.x,r.y,r.w,r.h,value,kCyan,kKeyFill,TFT_WHITE,g_layout.key_size);
    focus_nav::note(r.x,r.y,r.w,r.h);
  }
  const auto& cancel=g_layout.cancel;const auto& tune=g_layout.tune;
  key(cancel.x,cancel.y,cancel.w,cancel.h,"CANCEL",TFT_RED,kCardFill,TFT_RED,g_layout.action_size);
  key(tune.x,tune.y,tune.w,tune.h,"TUNE",kGreen,kGreen,TFT_BLACK,g_layout.action_size);
  focus_nav::note(cancel.x,cancel.y,cancel.w,cancel.h);
  focus_nav::note(tune.x,tune.y,tune.w,tune.h);
}

const Layout& layout() { return g_layout; }

bool key_point(char key, int* x, int* y) {
  if (!g_layout.valid) return false;
  for (int i = 0; i < 12; ++i) {
    if (kKeys[i] != key) continue;
    if (x) *x = g_layout.keys[i].x + g_layout.keys[i].w / 2;
    if (y) *y = g_layout.keys[i].y + g_layout.keys[i].h / 2;
    return true;
  }
  return false;
}

void cancel_point(int* x, int* y) {
  if (x) *x = g_layout.valid ? g_layout.cancel.x+g_layout.cancel.w/2 : -1;
  if (y) *y = g_layout.valid ? g_layout.cancel.y+g_layout.cancel.h/2 : -1;
}

void tune_point(int* x, int* y) {
  if (x) *x = g_layout.valid ? g_layout.tune.x+g_layout.tune.w/2 : -1;
  if (y) *y = g_layout.valid ? g_layout.tune.y+g_layout.tune.h/2 : -1;
}

Result handle_touch(int32_t x, int32_t y, char* entry, size_t entry_size) {
  if (!g_layout.valid || entry == nullptr || entry_size < 2) return Result::none;
  if (contains(g_layout.cancel,x,y)) return Result::cancelled;
  if (contains(g_layout.tune,x,y)) return Result::submitted;
  for (int i = 0; i < 12; ++i) {
    if (!contains(g_layout.keys[i],x,y)) continue;
    const size_t n = strlen(entry);
    if (kKeys[i] == '\b') {
      if (n) entry[n - 1] = '\0';
    } else if (n + 1 < entry_size && (kKeys[i] != '.' || strchr(entry, '.') == nullptr)) {
      entry[n] = kKeys[i];
      entry[n + 1] = '\0';
    }
    draw_field(entry);
    return Result::changed;
  }
  return Result::none;
}

bool self_check() {
  if (!g_layout.valid || !inside(g_layout.viewport,g_layout.card)) return false;
  for (const auto& r:g_layout.keys) if (!inside(g_layout.card,r)) return false;
  return inside(g_layout.card,g_layout.cancel) && inside(g_layout.card,g_layout.tune) &&
      g_layout.keys[11].y+g_layout.keys[11].h<g_layout.cancel.y &&
      g_layout.cancel.x+g_layout.cancel.w<g_layout.tune.x &&
      g_layout.field.y+g_layout.field.h<g_layout.keys[0].y;
}

}  // namespace orcsdr::freq_keypad
