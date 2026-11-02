// Host integration tests exercise the REAL existing lora_dashboard.cpp, not
// just a copied drawing layout. No firmware, display hardware, radio or RF IO.
// Linked stubs are intentionally minimal; no fake successful transmissions.
#include <M5Unified.h>
#include "lora_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"
#include "offline_map.hpp"
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
void note(int,int,int,int) {}
}
namespace orcsdr::offline_map {
bool available() { return false; }
void draw_base(const View&,uint16_t,uint16_t,uint16_t,uint16_t) {}
bool project(const View&,float,float,int*,int*) { return false; }
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
}
void landscape_navigation_and_revocation() {
  setup(1280,720);
  auto s=packet_profile();
  lora::show_documentation_view(lora::View::tx_lab,s);
  assert(lora::active()&&lora::view()==lora::View::tx_lab);
  assert(has("ARM LOCKED")&&has("SEND LOCKED"));
  assert(lora::handle_touch(100,560).kind==lora::ActionKind::none);
  assert(lora::handle_touch(430,560).kind==lora::ActionKind::none);
  const size_t prior=host_display::prims.size();
  lora::update(s);
  assert(host_display::prims.size()==prior); // no idle redraw

  host_display::reset();
  lora::handle_touch(950,190); // messages mode
  assert(has("COMPOSE PACKET")&&has("SEND LOCKED"));
  host_display::reset();
  lora::handle_touch(1120,190); // dual radio
  assert(has("RTL-SDR / IQ MONITOR"));
  assert(has("SX1262 / PACKET"));

  host_display::reset();
  s.tx.readiness=tx::Readiness::absent; // device disappeared
  s.revision++;
  lora::update(s);
  assert(lora::view()==lora::View::overview);
  assert(full_clears()==1);
  assert(lora::handle_touch(1200,670).kind==lora::ActionKind::select_view);
  assert(lora::handle_touch(1200,670).value==4); // 5 tabs, not hidden 6th
  host_display::reset();
  s.tx.readiness=tx::Readiness::profile_only; // radio reattached
  s.revision++;
  lora::update(s);
  assert(full_clears()==1); // rebuild navigation when radio comes back
  assert(lora::handle_touch(1200,670).value==5); // 6th tab is back
  std::puts("INTEGRATED_LANDSCAPE_REVOCATION_AND_REATTACH PASS");
}
void admitted_controls_still_only_return_actions() {
  setup(1280,720);
  auto s=packet_profile();
  s.tx.readiness=tx::Readiness::tx_verified;
  s.tx.antenna_path_verified=true;
  s.tx.regional_tx_profile_approved=true;
  s.tx.frequency_approved=true;
  s.tx.frequency_hz=915000000;
  s.tx.max_payload_bytes=24;
  lora::show_documentation_view(lora::View::tx_lab,s);
  assert(lora::handle_touch(120,559).kind==lora::ActionKind::tx_request_arm);
  assert(lora::handle_touch(450,559).kind==lora::ActionKind::none);
  s.tx.operator_armed=true;
  host_display::reset();
  lora::update(s);
  assert(has("DISARM TX")&&has("SEND TEST"));
  assert(full_clears()==0); // bounded updates only
  assert(lora::handle_touch(450,559).kind==lora::ActionKind::tx_send_test_packet);
  assert(lora::handle_touch(120,559).kind==lora::ActionKind::tx_request_disarm);
  s.tx.busy=true; // RF backend is busy
  lora::update(s);
  assert(lora::handle_touch(450,559).kind==lora::ActionKind::none);
  assert(lora::handle_touch(120,559).kind==lora::ActionKind::none);
  std::puts("INTEGRATED_TX_ACTION_GATES PASS");
}
void portrait_presence_and_ui() {
  setup(568,1232);
  auto s=packet_profile();
  lora::enter(s);
  assert(lora::view()==lora::View::tx_lab);
  assert(has("SX1262 PACKET TX")&&has("ARM LOCKED"));
  assert(full_clears()==1);
  assert(lora::handle_touch(120,1060).kind==lora::ActionKind::none);
  assert(lora::handle_touch(410,1060).kind==lora::ActionKind::none);
  assert(lora::handle_touch(489,50).kind==lora::ActionKind::exit_home);
  const size_t old=host_display::prims.size();
  lora::update(s);
  assert(host_display::prims.size()==old); // idle timer doesn't redraw
  host_display::reset();
  lora::handle_touch(230,185); // messages
  assert(has("PACKET COMPOSER"));
  host_display::reset();
  lora::handle_touch(490,185); // dual radio
  assert(has("RTL-SDR / IQ MONITOR"));
  assert(has("PROFILE ONLY"));

  host_display::reset();
  s.tx.readiness=tx::Readiness::absent;
  s.revision++;
  lora::update(s);
  assert(full_clears()==1);
  assert(has("SX1262 NOT AVAILABLE"));
  assert(!has("ARM TX")&&!has("SEND TEST"));
  assert(lora::handle_touch(120,1060).kind==lora::ActionKind::none);
  assert(lora::handle_touch(489,50).kind==lora::ActionKind::exit_home);
  host_display::reset();
  s.tx.readiness=tx::Readiness::profile_only; // radio returned, not TX-verified
  s.revision++;
  lora::update(s);
  assert(full_clears()==1 && has("SX1262 PACKET TX"));
  assert(has("ARM LOCKED") && has("SEND LOCKED"));
  assert(lora::handle_touch(120,1060).kind==lora::ActionKind::none);
  std::puts("INTEGRATED_PORTRAIT_REVOCATION_AND_REATTACH PASS");
}
void rx_only_regression() {
  setup(1280,720);
  lora::Snapshot s{};
  s.frequency_hz=906875000u;
  s.span_hz=500000u;
  s.bandwidth_hz=250000u;
  lora::show_documentation_view(lora::View::overview,s);
  assert(has("PASSIVE LORA MONITOR"));
  assert(!has("TX LAB"));
  assert(lora::handle_touch(1190,665).kind==lora::ActionKind::select_view);
  assert(lora::handle_touch(1190,665).value==4);
  assert(lora::handle_touch(110,560).kind==lora::ActionKind::none);
  std::puts("INTEGRATED_RX_ONLY PASS");
}
int main() {
  rx_only_regression();
  landscape_navigation_and_revocation();
  admitted_controls_still_only_return_actions();
  portrait_presence_and_ui();
  std::puts("LoRa TX full dashboard host simulation PASS");
}
