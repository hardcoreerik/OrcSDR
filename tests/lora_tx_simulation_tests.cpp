// Deterministic host simulation: actual TX view drawing functions, no TX hardware.
// Tests all 5 hardware readiness tiers, 32 combinations of gate conditions,
// 3 views, 2 orientations, and bounded touch mapping. Never radiates RF.
#include <M5Unified.h>
#include "lora_tx_views.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ui = orcsdr::lora::tx_views;
namespace tx = orcsdr::lora_tx;
using orcsdr::lora::Snapshot;

static bool has(const char* needle) {
  for (const auto& p : host_display::prims)
    if (p.kind == Prim::text && p.str.find(needle) != std::string::npos)
      return true;
  return false;
}

static void assert_within(int w, int h) {
  for (const auto& p : host_display::prims) {
    int x=0, y=0, pw=0, ph=0;
    if (!host_display::effective(p, &x, &y, &pw, &ph)) continue;
    if (x < 0 || y < 0 || x+pw > w || y+ph > h) {
      std::fprintf(stderr, "GFX overflow %dx%d (%d,%d %dx%d) '%s'\n",
                   w,h,x,y,pw,ph,p.str.c_str());
      assert(false);
    }
  }
}

static void exhaustive_state_matrix() {
  unsigned rendered = 0, permitted = 0, denied = 0;
  for (int rank=0; rank < 5; ++rank) {
    for (unsigned bits=0; bits < 32; ++bits) {
      Snapshot s{};
      s.tx.readiness = static_cast<tx::Readiness>(rank);
      s.tx.antenna_path_verified = (bits & 1) != 0;
      s.tx.regional_tx_profile_approved = (bits & 2) != 0;
      s.tx.frequency_approved = (bits & 4) != 0;
      s.tx.operator_armed = (bits & 8) != 0;
      s.tx.busy = (bits & 16) != 0;
      s.tx.frequency_hz = s.tx.frequency_approved ? 915000000u : 0u;
      s.tx.max_payload_bytes = 24;
      const bool may_arm = tx::can_arm(s.tx) == tx::Denial::none;
      const bool may_send = tx::can_send(s.tx, ui::kBenchPacketBytes) == tx::Denial::none;
      if (rank < 4) assert(!may_arm && !may_send);
      if (may_send) assert(may_arm && s.tx.operator_armed && !s.tx.busy);
      if (may_send) ++permitted; else ++denied;
      for (bool vertical : {false,true}) {
        for (int p=0;p<3;++p) {
          const auto page=static_cast<ui::Page>(p);
          host_display::reset();
          ui::draw_static(M5.Display,s,page,vertical);
          ui::menu(M5.Display,page,vertical);
          ui::draw_values(M5.Display,s,page,vertical);
          assert_within(vertical ? 568 : 1280,vertical ? 1232 : 720);
          if (may_arm) {
            assert(has(s.tx.operator_armed ? "DISARM TX" : "ARM TX"));
          } else {
            assert(has("ARM LOCKED"));
          }
          // Only Operator has a (still device-gated) bench packet send action.
          if (may_send && page == ui::Page::operator_panel)
            assert(has("SEND TEST") && !has("SEND LOCKED"));
          else assert(has("SEND LOCKED") && !has("SEND TEST"));
          if (rank < 4) assert(!has("CONDUCTED TX VERIFIED"));
          if (rank == 1) assert(has("PROFILE ONLY") || p==1);
          assert(!has("PACKET SENT") && !has("SUCCESS") && !has("TX READY"));
          ++rendered;
        }
      }
    }
  }
  assert(rendered == 960 && permitted > 0 && denied > permitted);
  std::printf("SIM_MATRIX rendered=%u permitted_states=%u denied_states=%u PASS\n",rendered,permitted,denied);
}

static void navigation_grid() {
  unsigned mapped_landscape=0,mapped_portrait=0;
  for (int y=0;y<1232;y+=7) {
    for(int x=0;x<1280;x+=7) {
      const int landscape=ui::page_at(x,y,false);
      const int portrait=ui::page_at(x,y,true);
      assert(landscape >= -1 && landscape <= 2);
      assert(portrait >= -1 && portrait <= 2);
      if (landscape != -1) {
        assert(y>=174 && y<222 && x>=680 && x<1222);
        ++mapped_landscape;
      }
      if (portrait != -1) {
        assert(y>=155 && y<211 && x>=16 && x<552);
        ++mapped_portrait;
      }
      if (ui::arm_rect(x,y,true)) assert(x>=28&&x<272&&y>=1026&&y<1098);
      if (ui::send_rect(x,y,true)) assert(x>=290&&x<534&&y>=1026&&y<1098);
      if (ui::arm_rect(x,y,false)) assert(x>=52&&x<338&&y>=540&&y<602);
      if (ui::send_rect(x,y,false)) assert(x>=356&&x<642&&y>=540&&y<602);
      assert(!(ui::arm_rect(x,y,true) && ui::send_rect(x,y,true)));
      assert(!(ui::arm_rect(x,y,false) && ui::send_rect(x,y,false)));
    }
  }
  assert(mapped_portrait > 0 && mapped_landscape > 0);
  // The P4 compat adapter returns its logical design size, not physical panel.
  // These checks make accidental "physical size" orientation assumptions fail.
  assert(ui::portrait(568,1232));
  assert(!ui::portrait(1280,720)); // current P4 default logical space
  assert(!ui::portrait(800,480));  // current Hosyond physical target
  assert(!ui::portrait(600,1300)); // unknown panel is NOT this native layout
  std::printf("SIM_TOUCH mapped_landscape=%u mapped_portrait=%u PASS\n",mapped_landscape,mapped_portrait);
}

static void dynamic_redraw_is_bounded() {
  Snapshot s{};
  s.tx.readiness = tx::Readiness::profile_only;
  for (bool vertical : {false,true}) {
    for(int p=0;p<3;p++) {
      host_display::reset();
      ui::draw_values(M5.Display,s,static_cast<ui::Page>(p),vertical);
      assert(!host_display::prims.empty());
      for(const auto& prim : host_display::prims) {
        int x=0,y=0,w=0,h=0;
        if(!host_display::effective(prim,&x,&y,&w,&h))continue;
        assert(x >= (vertical ? 28 : 65));
        assert(y >= (vertical ? (p==0 ? 500 : p==1 ? 630 : 730) : 300));
        assert(x+w <= (vertical ? 542 : 1220));
        assert(y+h <= (vertical ? 1125 : 525));
      }
    }
  }
  std::puts("SIM_PARTIAL_REDRAW PASS");
}

int main() {
  exhaustive_state_matrix();
  navigation_grid();
  dynamic_redraw_is_bounded();
  std::puts("LoRa TX software-only GFX + TX safety simulation PASS");
}
