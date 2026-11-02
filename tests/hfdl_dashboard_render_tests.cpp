#include <cassert>
#include <cstdio>
#include "M5Unified.h"
#include "../apps/orcsdr-tab5/ui/hfdl_dashboard.hpp"
#include "../apps/orcsdr-tab5/ui/dashboard_audio_control.hpp"

HfdlFakeM5 M5;
namespace orcsdr::audio_header {
void draw_brand(const char*){ M5.Display.drawString("ORCSDR",20,40); }
}
using namespace orcsdr;
static bool overlay=false;
static bool overlay_open(){return overlay;}
static int headers=0;
static void shared_header(){++headers;}
static void test_panel(int w,int h){
  M5.Display=HfdlFakeDisplay{};
  M5.Display.screen_width=w;
  M5.Display.screen_height=h;
  hfdl_ui::Model model;
  hfdl_ui::Controller ui;
  hfdl_dashboard::install(&model,&ui,shared_header);
  hfdl_dashboard::set_overlay_probe(overlay_open);
  hfdl_dashboard::enter();
  assert(hfdl_dashboard::active());
  assert(hfdl_dashboard::viewport().valid);
  assert(M5.Display.full_clears==1);
  const auto initial=M5.Display.region_paints;
  hfdl_dashboard::update(10);
  assert(M5.Display.full_clears==1);
  assert(M5.Display.region_paints==initial);
  model.post_receiver_state(hfdl_ui::ReceiverState::running);
  hfdl_dashboard::update(20);
  assert(M5.Display.region_paints>initial);
  assert(M5.Display.full_clears==1);
  hfdl_ui::MessageRecord msg{};
  msg.frequency_hz=10081000;
  std::snprintf(msg.preview,sizeof(msg.preview),"CRC-verified fixture");
  assert(model.post_message(msg));
  const auto previous=M5.Display.region_paints;
  overlay=true;
  hfdl_dashboard::update(25);
  assert(M5.Display.region_paints==previous);
  overlay=false;
  hfdl_dashboard::update(30);
  assert(M5.Display.region_paints>previous);
  assert(M5.Display.full_clears==1);
  const auto& view=hfdl_dashboard::viewport();
  assert(hfdl_dashboard::touch(view.rows[0].x+5,view.rows[0].y+5).kind==
         hfdl_ui::ActionKind::changed);
  assert(ui.detail_open());
  assert(M5.Display.full_clears==1);
  assert(hfdl_dashboard::touch(view.close.x+5,view.close.y+5).kind==
         hfdl_ui::ActionKind::changed);
  assert(!ui.detail_open());
  assert(M5.Display.full_clears==1);
  hfdl_dashboard::leave();
  hfdl_dashboard::enter();
  assert(M5.Display.full_clears==2); // new entry is a structural transition
  hfdl_dashboard::leave();
}
int main(){
  test_panel(1280,720);
  test_panel(720,720);
  test_panel(800,400);
  test_panel(568,1232);
  std::puts("hfdl_dashboard_render_test PASS (four M5GFX layouts; no idle full clears)");
}
