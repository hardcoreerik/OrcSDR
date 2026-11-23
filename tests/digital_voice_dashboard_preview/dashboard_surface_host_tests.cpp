#include "digital_voice_dashboard_surface.hpp"
#include "screen_controller.hpp"
#include "digital_voice_dashboard_demo.hpp"
#ifdef NDEBUG
#undef NDEBUG // Conformance assertions are always active, including Release.
#endif
#include <cassert>
#include <cstdio>
using namespace orcsdr::digital_voice_ui;
// Test double for the real ScreenController ownership query: only the Digital
// Voice id can be owned, and only while the test says the controller granted it.
// Ownership enforcement in Surface stays live; the real controller pulls in the
// whole app, so it is not linked into this host suite.
namespace orcsdr::screens {
bool g_owns=false;
bool owns(Id id){ return id==Id::digital_voice && g_owns; }
}
namespace {int headers=0;void header(){++headers;}}
int main(){
  lgfx::LovyanGFX display;
  Surface ui;
  auto s=demo::make(demo::Case::disconnected);
  ui.enter();host_stub::reset();
  assert(!ui.draw(display,s,false,header));
  assert(!ui.active() && headers==0 && host_stub::calls.empty());
  orcsdr::screens::g_owns=true;
  assert(!ui.draw(display,s,true,header) && headers==0);
  assert(ui.draw(display,s,false,header) && headers==1 && ui.active());
  assert(ui.draw(display,s,false,header) && headers==1); // no redrawing header on ticks
  assert(ui.touch(30,675,s,true).kind==ActionKind::none); // overlays own touch
  assert(ui.touch(35,100,s,false).kind==ActionKind::none); // header owned by app
  assert(ui.touch(256,675,s,false).kind==ActionKind::none); // local LIVE navigation
  assert(ui.state().view==View::live);
  assert(ui.update(display,s,false));
  assert(ui.touch(1074,678,s,false).kind==ActionKind::none); // switch to SETUP
  assert(ui.state().view==View::setup);
  assert(ui.update(display,s,false));
  assert(ui.touch(90,555,s,false).kind==ActionKind::none);
  assert(ui.state().density==Density::analysis_first);
  auto scan=s;scan.can_scan=true;scan.receiver=Receiver::ready;
  assert(ui.touch(65,675,s,false).kind==ActionKind::none);
  assert(ui.state().view==View::explore);
  assert(ui.update(display,scan,false));
  assert(ui.touch(60,577,scan,false).kind==ActionKind::scan); // returned, no IO performed
  orcsdr::screens::g_owns=false;
  assert(!ui.update(display,scan,false));
  assert(ui.touch(60,577,scan,false).kind==ActionKind::none);
  ui.leave();assert(!ui.active());
  std::puts("PASS ScreenController owner, shared header, overlay, navigation and scan dispatch");
}
