// Host integration tests exercise the REAL existing lora_dashboard.cpp, not
// just a copied drawing layout. No firmware, display hardware, radio or RF IO.
// Linked stubs are intentionally minimal; no fake successful transmissions.
#include <M5Unified.h>
#include "lora_dashboard.hpp"
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
RegionalView draw_camera(lgfx::v1::LovyanGFX&,const orcmap::Viewport&,int,int,uint16_t,uint16_t,uint16_t,const char*,bool) {return {};}
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
void native_safe_views() {
  for(bool vertical:{false,true}) {
    setup(vertical?568:1232,vertical?1232:568);
    const orcsdr::audio_header::HeaderRect safe{72,72,vertical?424:1088,vertical?1088:424};
    const auto layout=lora::tx_native::make(safe);assert(layout.valid);
    assert(lora::set_tx_viewport(safe));
    auto s=packet_profile();lora::enter(s);
    for(int page=0;page<3;++page) {
      host_display::reset();
      const auto mode=layout.mode(page);
      lora::handle_touch(mode.x+mode.w/2,mode.y+mode.h/2);
      lora::draw();
      assert(has("PROFILE ONLY")&&has("ARM LOCKED")&&has("SEND LOCKED"));
      if(!vertical&&page==0)assert(has("MODULATION: LORA / SX1262"));
      std::vector<Prim> text_boxes;
      for(const auto& p:host_display::prims) {
        if(p.w<=0||p.h<=0)continue;
        if(p.kind==Prim::text)assert(p.h>=16); // no unreadable 8-pixel native labels
        if(!(p.x>=safe.x&&p.y>=safe.y&&p.x+p.w<=safe.x+safe.w&&p.y+p.h<=safe.y+safe.h)) {
          std::fprintf(stderr,"native bounds %s: %s [%d,%d %dx%d]\n",vertical?"portrait":"landscape",p.str.c_str(),p.x,p.y,p.w,p.h);assert(false);
        }
        if(p.kind==Prim::text) {
          for(const auto& before:text_boxes) {
            if(before.x==p.x&&before.y==p.y&&before.str==p.str)continue;
            const bool overlap=p.x<before.x+before.w&&before.x<p.x+p.w&&p.y<before.y+before.h&&before.y<p.y+p.h;
            if(overlap)std::fprintf(stderr,"native text overlap: %s / %s\n",before.str.c_str(),p.str.c_str());
            assert(!overlap);
          }
          text_boxes.push_back(p);
        }
      }
      for(bool send:{false,true}) {
        const auto r=layout.action(send);assert(r.w>=44&&r.h>=44);
        assert(lora::handle_touch(r.x+r.w/2,r.y+r.h/2).kind==lora::ActionKind::none);
      }
      host_display::reset();s.revision++;lora::update(s);
      assert(host_display::prims.empty()); // receiver revisions cannot flash TX cards
    }
    host_display::reset();s.tx.readiness=tx::Readiness::absent;lora::update(s);
    assert(has("NO RADIO"));
    s.tx.readiness=tx::Readiness::profile_only;lora::update(s);
    assert(has("SX1262 PACKET TX")||has("LORA / SX1262 TX LAB"));
  }
  assert(lora::set_tx_viewport({0,0,1280,720}));
  std::puts("INTEGRATED_NATIVE_SAFE_TX_VIEWS PASS");
}
void message_only_redraws() {
  struct Case { int w,h; bool native; };
  for (const auto c : {Case{1280,720,false},Case{568,1232,false},
                       Case{1232,568,true},Case{568,1232,true}}) {
    setup(c.w,c.h);
    auto s=packet_profile();
    if(c.native) {
      const orcsdr::audio_header::HeaderRect safe{72,72,c.w-144,c.h-144};
      assert(lora::set_tx_viewport(safe));
      lora::enter(s);
    } else if(c.w==1280) {
      lora::show_documentation_view(lora::View::tx_lab,s);
    } else {
      lora::enter(s);
    }
    assert(lora::select_tx_page(1));
    host_display::reset();
    s.event_count=1;
    auto& e=s.events[0];e.verified=true;e.sender=0x1234;e.packet_id=1;e.received_ms=100;
    std::snprintf(e.text,sizeof(e.text),"First verified mesh message");
    ++s.revision;lora::update(s);
    assert(has("First verified"));
    assert(full_clears()==0);
    assert(host_display::prims.size()<12); // one card value region, no chrome/actions

    host_display::reset();
    e.packet_id=2;e.received_ms=200;
    std::snprintf(e.text,sizeof(e.text),"Second verified message replaces the first");
    ++s.revision;lora::update(s);
    assert(has("Second verified"));
    assert(!has("First verified"));
    assert(full_clears()==0);

    host_display::reset();
    e.verified=false;
    ++s.revision;lora::update(s);
    assert(has("UNVERIFIED EVENT / TEXT HIDDEN"));
    assert(!has("Second verified"));
    assert(full_clears()==0);
  }
  std::puts("INTEGRATED_MESSAGE_ONLY_REDRAWS PASS");
}
int main() {
  rx_only_regression();
  landscape_navigation_and_revocation();
  admitted_controls_still_only_return_actions();
  portrait_presence_and_ui();
  native_safe_views();
  message_only_redraws();
  std::puts("LoRa TX full dashboard host simulation PASS");
}
