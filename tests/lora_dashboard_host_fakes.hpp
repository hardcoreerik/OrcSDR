#pragma once
// Neutral host dependencies shared by the actual LoRa renderer tests.
// Host integration tests exercise the REAL existing lora_dashboard.cpp, not
// just a copied drawing layout. No firmware, display hardware, radio or RF IO.
// Linked stubs are intentionally minimal; no fake successful transmissions.
#include <M5Unified.h>
#include "lora_dashboard.hpp"
#include "lora_tx_views.hpp"
#include "lora_tx_native_layout.hpp"
#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"
#include "map_view.hpp"
#include "map_preferences.hpp"
#include "receiver_location.hpp"
#include "screen_controller.hpp"
#include "waterfall_style.hpp"
#include "lora_channel_control.hpp"
#include "orc_badge.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

// Neutral pixels in the badge binary, provided only to satisfy the real
// drawing path. The firmware uses its actual embedded OrcSDR badge asset.
asm(".balign 2\n"
    ".global _binary_orc_badge_104_rgb565_start\n"
    "_binary_orc_badge_104_rgb565_start:\n"
    ".zero 21632\n"
    ".global _binary_orc_badge_104_rgb565_end\n"
    "_binary_orc_badge_104_rgb565_end:\n");

namespace orcsdr::audio_header {
void draw_brand(const char*) {}
void draw_home_button() {}
void draw_settings_button() {}
void draw_mute_button(bool) {}
void draw_visualizer_button(bool) {}
void draw_battery(int32_t) {}
bool home_hit(int32_t x, int32_t y) { return x >= 1040 && x < 1098 && y < 66; }
bool settings_hit(int32_t x, int32_t y) { return x >= 1217 && x < 1268 && y < 66; }
bool self_check() { return true; }
}
namespace orcsdr::focus_nav {
void clear() {}
void note(int,int,int,int) {}
bool set_viewport(int,int) {return true;}
}
namespace orcsdr::screens {bool owns(Id) {return true;}}
namespace orcsdr::receiver_location {Snapshot snapshot() {return {};}}
namespace orcsdr::maps::preferences {
bool labels(Consumer) {return false;}
bool set_labels(Consumer,bool) {return false;}
}
namespace orcsdr::maps {
uint32_t revision() {return 0;}
bool regional_view(int32_t lat,int32_t lon,double,int w,int h,orcmap::Viewport* out) {
  out->width_px=w;out->height_px=h;return orcmap::SetCenter(out,lat/1e7,lon/1e7);
}
RegionalView draw_camera(lgfx::v1::LovyanGFX&,const orcmap::Viewport&,int,int,uint16_t,uint16_t,uint16_t,const char*,bool,unsigned) {return {};}
bool project(const RegionalView&,int32_t,int32_t,int*,int*) {return false;}
}
namespace orcsdr::waterfall_style {
uint8_t palette(Screen) { return 0; }
uint8_t speed(Screen) { return 0; }
void next_palette(Screen) {}
void next_speed(Screen) {}
const char* palette_name(uint8_t) { return "CLASSIC"; }
const char* speed_name(uint8_t) { return "NORMAL"; }
int rows_due(Screen,uint32_t,uint32_t) { return 0; }
uint16_t color565(Screen,float) { return 0; }
}
namespace orcsdr::lora_channel {
uint8_t survey_span_count() { return 14; }
bool self_check() { return true; }
}

namespace lora = orcsdr::lora;
namespace tx = orcsdr::lora_tx;

bool has(const char* needle) {
  for (const auto& p: host_display::prims)
    if (p.kind == Prim::text && p.str.find(needle) != std::string::npos) return true;
  return false;
}
int full_clears() {
  int count=0;
  for(const auto& p:host_display::prims)
    if(p.kind==Prim::fill&&p.x==0&&p.y==0&&p.w==M5.Display.width()&&p.h==M5.Display.height())
      ++count;
  return count;
}
lora::Snapshot packet_profile() {
  lora::Snapshot s{};
  s.tx.readiness=tx::Readiness::profile_only;
  s.frequency_hz=906875000u;
  s.span_hz=500000u;
  s.sf=11;
  s.bandwidth_hz=250000u;
  std::snprintf(s.board_label,sizeof(s.board_label),"LILYGO T-DISPLAY P4");
  s.running=true;
  return s;
}
void setup(int w,int h) {
  host_display::design_width=w;
  host_display::design_height=h;
  host_display::reset();
  lora::leave();
  assert(lora::set_tx_viewport({0,0,w,h}));
}
