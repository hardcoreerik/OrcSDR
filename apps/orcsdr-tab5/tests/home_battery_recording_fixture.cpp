#include "home_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "freq_keypad.hpp"
#include "waterfall_style.hpp"
#include <M5Unified.h>

#include <cassert>
#include <cstdio>

int home_battery_draws = 0;
int home_battery_last = -999;

namespace orcsdr::dashboards {
namespace {
constexpr Descriptor entries[] = {
    {Id::fm,"FM","Radio",Category::audio,true},
    {Id::p25,"P25","Digital voice",Category::digital,true},
};
}
const Descriptor* find(Id id) { for (const auto& e: entries) if(e.id==id) return &e; return nullptr; }
const Descriptor* descriptor(size_t i) { return i<2?&entries[i]:nullptr; }
size_t count() { return 2; }
void load_recent(const uint8_t*,size_t) {}
bool record_open(Id) { return true; }
size_t recent_count() { return 0; }
Id recent(size_t) { return Id::home; }
size_t copy_recent(uint8_t*,size_t) { return 0; }
bool self_check() { return true; }
}

namespace orcsdr::audio_header {
namespace { HeaderLayout current=make_header_layout({0,0,1280,720}); }
bool set_viewport(HeaderRect v) { current=make_header_layout(v); return current.valid; }
const HeaderLayout& layout() { return current; }
void reset(Control&) {}
void draw_badge() {}
void draw_brand(const char*) {}
void draw_battery(int32_t p) { ++home_battery_draws; home_battery_last=p; const auto r=current.battery; M5.Display.fillRect(r.x,r.y,r.w,r.h,TFT_BLACK); }
void draw(const Control&,uint8_t,bool,int32_t p) { draw_battery(p); }
void capture_volume_background() {}
void restore_volume_background() {}
void draw_home_button() {}
bool home_hit(int32_t x,int32_t y) { const auto r=current.home; return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h; }
void draw_mute_button(bool) {}
bool mute_hit(int32_t,int32_t) { return false; }
void draw_visualizer_button(bool) {}
bool visualizer_hit(int32_t,int32_t) { return false; }
void draw_settings_button() {}
bool settings_hit(int32_t,int32_t) { return false; }
Action handle_touch(Control&,int32_t,int32_t,uint32_t,uint8_t) { return {}; }
bool service_timeout(Control&,uint32_t) { return false; }
bool self_check() { return true; }
}

namespace orcsdr::freq_keypad {
void draw(int,uint16_t,const char*,const char*,const char*,const char*,const Rect*) {}
const Layout& layout() { static Layout v{}; return v; }
Result handle_touch(int32_t,int32_t,char*,size_t) { return Result::cancelled; }
bool key_point(char,int*,int*) { return false; }
void cancel_point(int* x,int* y) { *x=250;*y=650; }
void tune_point(int* x,int* y) { *x=800;*y=650; }
bool self_check() { return true; }
}

namespace orcsdr::waterfall_style {
uint8_t default_palette(Screen) { return 0; }
uint8_t palette(Screen) { return 0; }
uint8_t speed(Screen) { return 0; }
void set_palette(Screen,uint8_t) {}
void set_speed(Screen,uint8_t) {}
void next_palette(Screen) {}
void next_speed(Screen) {}
const char* palette_name(uint8_t) { return "TEST"; }
const char* speed_name(uint8_t) { return "TEST"; }
int rows_per_frame(Screen) { return 1; }
void reset_clock(Screen,uint32_t) {}
int rows_due(Screen,uint32_t,uint32_t) { return 0; }
uint16_t color565(Screen,float) { return 0; }
uint16_t color565(uint8_t,float) { return 0; }
uint8_t pack(Screen) { return 0; }
void unpack(Screen,uint8_t) {}
void set_persist_hook(PersistHook) {}
}

static void expect_battery(int value, bool expected_draw) {
  const int before=home_battery_draws, clears=M5.Display.full_clears;
  const size_t fills=M5.Display.fills.size();
  auto snapshot=orcsdr::home::Snapshot{};
  snapshot.revision=77; // deliberately unchanged across battery-only events
  snapshot.battery_percent=value;
  orcsdr::home::update(snapshot);
  assert(home_battery_draws==before+(expected_draw?1:0));
  assert(M5.Display.full_clears==clears);
  assert(M5.Display.fills.size()==fills+(expected_draw?1:0));
  if(expected_draw) {
    assert(home_battery_last==value);
    const auto r=orcsdr::audio_header::layout().battery;
    const auto fill=M5.Display.fills.back();
    assert(fill.x==r.x&&fill.y==r.y&&fill.w==r.w&&fill.h==r.h);
  }
}

static void tap(orcsdr::home::LayoutRect r) {
  const int x=r.x+r.w/2,y=r.y+r.h/2;
  orcsdr::home::handle_touch(x,y,true);
  orcsdr::home::handle_touch(x,y,false);
}

static void expect_modal_update(int value, bool expected_draw) {
  const int before=home_battery_draws,clears=M5.Display.full_clears;
  const size_t fills=M5.Display.fills.size();
  auto snapshot=orcsdr::home::Snapshot{};snapshot.revision=77;snapshot.battery_percent=value;
  orcsdr::home::update(snapshot);
  assert(home_battery_draws==before+(expected_draw?1:0));
  assert(M5.Display.full_clears==clears);
  assert(M5.Display.fills.size()==fills+(expected_draw?1:0));
}

static void check_viewport(orcsdr::home::LayoutRect viewport) {
  using namespace orcsdr;
  home::leave();
  assert(home::set_viewport(viewport));
  auto initial=home::Snapshot{};initial.revision=77;initial.battery_percent=30;
  home::enter(initial);
  M5.Display.reset();home_battery_draws=0;
  expect_battery(31,true);
  expect_battery(31,false);
  assert(audio_header::header_inside(viewport,audio_header::layout().battery));
}

int main() {
  auto initial=orcsdr::home::Snapshot{};
  initial.revision=77; initial.battery_percent=43;
  orcsdr::home::enter(initial);
  M5.Display.reset(); home_battery_draws=0; home_battery_last=-999;
  expect_battery(44,true);
  expect_battery(44,false);
  expect_battery(-1,true);
  expect_battery(-1,false);

  auto newer=initial;newer.revision=78;newer.battery_percent=-1;
  orcsdr::home::update(newer);
  assert(home_battery_draws==2); // generic revision change with unchanged battery is quiet

  // Lower popups leave the header battery region uncovered.
  tap(orcsdr::home::layout().gain);
  assert(orcsdr::home::popup_open());
  expect_modal_update(56,true);
  orcsdr::home::close_popup();

  // The keypad owns the display: cache its incoming value and show it on return.
  tap(orcsdr::home::layout().frequency);
  assert(orcsdr::home::keypad_open());
  const int keypad_before=home_battery_draws;
  auto keypad_value=orcsdr::home::Snapshot{};keypad_value.revision=77;keypad_value.battery_percent=67;
  orcsdr::home::update(keypad_value);
  assert(home_battery_draws==keypad_before);
  tap({220,630,840,72}); // cancel control handled by keypad test double
  assert(!orcsdr::home::keypad_open());
  assert(home_battery_last==67);

  // The browser owns the display but retains updates for its Home return.
  tap(orcsdr::home::layout().all);
  assert(orcsdr::home::browser_active());
  const int browser_before=home_battery_draws;
  auto browser_value=orcsdr::home::Snapshot{};browser_value.revision=77;browser_value.battery_percent=82;
  orcsdr::home::update(browser_value);
  assert(home_battery_draws==browser_before);
  tap(orcsdr::audio_header::layout().home);
  assert(!orcsdr::home::browser_active());
  assert(home_battery_last==82);

  check_viewport({0,0,1280,720});
  check_viewport({0,0,800,480});
  check_viewport({72,72,1088,424});
  check_viewport({72,72,424,1088});
  std::puts("HOME_BATTERY_RECORDING_PASS");
}
