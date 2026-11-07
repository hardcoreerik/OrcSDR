// Compiles the production AIS dashboard against the same M5GFX recording stand-in used by ACARS/Dead Air.
// No AIS packet is manufactured by the production renderer; fixtures below are exclusively test inputs.
#include <M5Unified.h>
#include "ais_dashboard.hpp"
#include "dashboard_audio_control.hpp"
#include "screen_controller.hpp"
#include "focus_nav.hpp"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>
namespace ah=orcsdr::audio_header;
namespace ais=orcsdr::ais_ui;
namespace screens=orcsdr::screens;
namespace orcsdr::focus_nav {
void note(int,int,int,int) {}
void clear() {}
}
namespace orcsdr::audio_header {
void draw_brand(const char* s) {
  M5.Display.setFont(nullptr);M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawString("OrcSDR",128,38);
  M5.Display.drawString(s,128,82);
}
void draw_battery(int32_t) {}
void draw_home_button() { M5.Display.drawRoundRect(1040,8,58,58,8,TFT_WHITE); }
void draw_mute_button(bool) {}
void draw_visualizer_button(bool) {}
void draw_settings_button() { M5.Display.drawRoundRect(1217,12,51,54,8,TFT_WHITE); }
bool home_hit(int32_t x,int32_t y) {return x>=1040&&x<1098&&y>=8&&y<66;}
bool settings_hit(int32_t x,int32_t y){return x>=1217&&x<1268&&y>=12&&y<66;}
}
static bool has_text(const char* expected) {
  for (const Prim& p : host_display::prims)
    if(p.kind==Prim::text&&p.str.find(expected)!=std::string::npos)return true;
  return false;
}
static void bounds() {
  for(const Prim& p:host_display::prims) {
    int x,y,w,h;
    if(!host_display::effective(p,&x,&y,&w,&h))continue;
    assert(x>=0&&y>=0&&x+w<=1280&&y+h<=720);
    // Body content is never painted over reserved status-bar x420..866,y12..66.
    if(y>=114)assert(!(y<66));
  }
}
static void switch_to(ais::View page,const char* name) {
  host_display::reset();
  const int i=static_cast<int>(page);
  const auto action=ais::handle_touch(20+i*178+20,675);
  assert(action.kind==ais::ActionKind::none&&ais::view()==page);
  assert(has_text(name));
  bounds();
}
int main(){
  assert(ais::self_check());
  // Real data contract: a disconnected receiver must produce empty-state UI, not decorative samples.
  static ais::Snapshot empty;
  empty.revision=1;
  screens::begin_transition(screens::Id::ais,100);
  ais::enter(empty);
  screens::finish_transition();
  assert(ais::active()&&ais::view()==ais::View::map);
  assert(has_text("LOCAL MARITIME MAP"));
  assert(has_text("RECEIVER DISCONNECTED"));
  assert(!has_text("123456789"));
  bounds();

  switch_to(ais::View::targets,"AIS STATIONS");
  assert(has_text("NO AIS STATIONS DECODED"));
  switch_to(ais::View::messages,"LIVE AIS MESSAGE JOURNAL");
  assert(has_text("NO VALIDATED MESSAGES YET"));
  switch_to(ais::View::protocol,"ALL 28 AIS MESSAGE TYPES");
  assert(ais::protocol_type()==1);
  assert(ais::handle_touch(200,591).kind==ais::ActionKind::none);
  auto a=ais::handle_touch(100,257);
  assert(a.kind==ais::ActionKind::set_message_type);
  switch_to(ais::View::signal,"RECEPTION DIAGNOSTICS");
  assert(has_text("NO MEASURED FFT DATA"));
  switch_to(ais::View::events,"MARITIME ACTIVITY");
  assert(has_text("NO EVENTS OBSERVED"));
  switch_to(ais::View::setup,"RECEIVER / RF");
  assert(ais::handle_touch(85,243).kind==ais::ActionKind::toggle_ais1);
  assert(ais::handle_touch(85,306).kind==ais::ActionKind::toggle_ais2);
  assert(ais::handle_touch(687,243).kind==ais::ActionKind::toggle_nmea);
  assert(ais::handle_touch(687,306).kind==ais::ActionKind::toggle_logging);
  assert(ais::handle_touch(1060,33).kind==ais::ActionKind::close);

  // Independently supplied valid test data; nonexistent fields must stay unknown.
  static ais::Snapshot fixture;
  fixture.revision=2;fixture.receiver=ais::ReceiveState::ready;fixture.receiver_settled=true;
  fixture.target_count=1;fixture.targets[0].mmsi=123456789;
  fixture.targets[0].station=ais::Station::aid_virtual;
  std::strcpy(fixture.targets[0].name,"VIRTUAL MARKER");
  fixture.targets[0].heard_age_s=4;
  fixture.message_count=1;fixture.messages[0].type=21;fixture.messages[0].mmsi=123456789;
  fixture.messages[0].channel=2;fixture.messages[0].crc_ok=true;
  std::strcpy(fixture.messages[0].summary,"TEST AtoN MESSAGE");
  fixture.events[0].type=14;fixture.events[0].safety_related=true;
  std::strcpy(fixture.events[0].title,"TEST SAFETY BROADCAST");
  fixture.event_count=1;
  ais::update(fixture);
  switch_to(ais::View::targets,"AIS STATIONS");
  a=ais::handle_touch(55,265);assert(a.kind==ais::ActionKind::target_selected&&a.value==123456789);
  assert(has_text("VIRTUAL AtoN"));
  assert(a.identity_kind==ais::IdentityKind::mmsi);
  // Collision case: the 2026 Type 28 Source ID has the same numeric
  // value as an unrelated station MMSI; selection and label must stay distinct.
  fixture.revision=3;fixture.target_count=2;
  auto& type28=fixture.targets[1];
  type28.identity_kind=ais::IdentityKind::source_id28;
  type28.source_id28=123456789;
  type28.mmsi=0;
  type28.station=ais::Station::aid_2026;
  type28.has_position=true;
  type28.lat_e7=474000000;type28.lon_e7=-1223000000;
  std::strcpy(type28.name,"TYPE 28 REEF");
  fixture.message_count=2;
  auto& type28_message=fixture.messages[1];
  type28_message.type=28;type28_message.channel=1;
  type28_message.identity_kind=ais::IdentityKind::source_id28;
  type28_message.source_id28=123456789;
  type28_message.crc_ok=true;
  std::strcpy(type28_message.summary,"2026 AtoN TEST FIXTURE");
  ais::update(fixture);
  a=ais::handle_touch(55,308);
  assert(a.kind==ais::ActionKind::target_selected);
  assert(a.value==123456789&&a.identity_kind==ais::IdentityKind::source_id28);
  assert(has_text("SOURCE ID"));
  assert(has_text("2026 AtoN"));
  switch_to(ais::View::messages,"LIVE AIS MESSAGE JOURNAL");
  a=ais::handle_touch(56,300);
  assert(a.kind==ais::ActionKind::message_selected&&a.value==1);
  assert(has_text("SOURCE ID 123456789"));
  assert(has_text("TEST AtoN MESSAGE"));
  switch_to(ais::View::events,"MARITIME ACTIVITY");
  assert(has_text("TEST SAFETY BROADCAST"));
  // No leakage when a selected target disappears.
  fixture.revision=4;fixture.target_count=0;
  ais::update(fixture);
  switch_to(ais::View::targets,"AIS STATIONS");
  assert(has_text("TAP A ROW TO INSPECT"));
  // Missing receiver silently must not remain active on the drawing surface.
  screens::begin_transition(screens::Id::home,600);
  screens::finish_transition();
  host_display::reset();
  fixture.revision=5;
  ais::update(fixture);
  assert(host_display::prims.empty());
  ais::leave();
  assert(!ais::active());
  std::puts("AIS seven-panel host render / bounds / touch / no-fabrication tests passed");
}
