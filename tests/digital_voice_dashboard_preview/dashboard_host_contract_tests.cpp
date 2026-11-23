#include "digital_voice_dashboard.hpp"
#include "digital_voice_dashboard_demo.hpp"
#include <cassert>
#include <cstdio>
#include <string>
namespace dv=orcsdr::digital_voice_ui;
using namespace dv;
int main(){
  lgfx::LovyanGFX display;
  Dashboard dashboard;
  UiState ui{};
  host_stub::reset();
  auto s=demo::make(demo::Case::disconnected);
  assert(!dashboard.present(display,s,ui,false,false));
  assert(host_stub::calls.empty());
  assert(dashboard.present(display,s,ui,true,false));
  assert(host_stub::contains("RECEIVER DISCONNECTED"));
  assert(host_stub::contains("DISCOVERY FIRST"));
  assert(host_stub::contains("DIGITAL VOICE SCANNER NOT CONNECTED"));
  assert(host_stub::contains("SCAN"));
  assert(dashboard.handle_touch(55,598,s,ui).kind==ActionKind::none); // disabled scan never initiates tuning
  assert(host_stub::full_clear_count==0 && !host_stub::out_of_bounds);
  const size_t count=host_stub::calls.size();
  assert(dashboard.present(display,s,ui,true,false));
  assert(host_stub::calls.size()==count); // no unchanged redraw
  assert(!dashboard.present(display,s,ui,true,true));
  assert(host_stub::calls.size()==count); // overlay block
  s=demo::make(demo::Case::dmr_replay);s.revision=2;
  assert(dashboard.present(display,s,ui,true,false));
  assert(host_stub::contains("RECORDED IQ REPLAY"));
  assert(host_stub::contains("HOST VALIDATED / NOT LIVE TAB5"));
  assert(!host_stub::out_of_bounds && host_stub::full_clear_count==0);
  // Scanner capability is explicit and independent of protocol or identity lock.
  auto scan=s;
  scan.revision=7;
  scan.receiver=Receiver::ready;
  scan.can_scan=true;
  scan.scan.phase=ScanPhase::scanning;
  scan.scan.progress_valid=true;
  scan.scan.visited=3;
  scan.scan.planned=12;
  scan.scan.validated_hits=0;
  host_stub::reset();assert(dashboard.present(display,scan,ui,true,false));
  assert(host_stub::contains("SCAN RUNNING"));
  assert(host_stub::contains("CHANNEL 3 / 12  VERIFIED 0"));
  assert(dashboard.handle_touch(55,598,scan,ui).kind==ActionKind::scan);
  ui.view=View::live;
  host_stub::reset();
  assert(dashboard.present(display,s,ui,true,false));
  assert(host_stub::contains("3024109"));
  assert(host_stub::contains("99"));
  assert(host_stub::contains("COLOR CODE"));
  assert(host_stub::contains("TIME SLOT"));
  assert(host_stub::contains("UNKNOWN"));
  assert(!host_stub::contains("TS1") && !host_stub::contains("TS2"));
  assert(!host_stub::out_of_bounds && host_stub::full_clear_count==0);
  auto x=s; x.revision=3; x.evidence=Evidence::candidate;
  host_stub::reset();assert(dashboard.present(display,x,ui,true,false));
  assert(host_stub::contains("SOURCE") && host_stub::contains("UNKNOWN"));
  assert(!host_stub::contains("3024109"));
  assert(host_stub::full_clear_count==0); // value update not screen clear
  ui.density=Density::analysis_first;
  host_stub::reset();assert(dashboard.present(display,s,ui,true,false));
  assert(host_stub::contains("ANALYSIS FIRST"));
  assert(host_stub::contains("INTEGRITY"));
  assert(!host_stub::out_of_bounds);
  ui.view=View::signal;
  auto none=demo::make(demo::Case::signal_empty);none.revision=4;
  host_stub::reset();assert(dashboard.present(display,none,ui,true,false));
  assert(host_stub::contains("NO VERIFIED MEASUREMENTS"));
  assert(!host_stub::contains("REL dB"));
  ui.view=View::systems;
  host_stub::reset();assert(dashboard.present(display,none,ui,true,false));
  assert(host_stub::contains("SYSTEM LEARNING NOT INTEGRATED"));
  ui.view=View::last_heard;
  host_stub::reset();assert(dashboard.present(display,none,ui,true,false));
  assert(host_stub::contains("HISTORY NOT INTEGRATED"));
  assert(!host_stub::out_of_bounds);
  auto hist=demo::make(demo::Case::history_demo);hist.revision=5;
  host_stub::reset();assert(dashboard.present(display,hist,ui,true,false));
  assert(host_stub::contains("ID VERIFIED"));
  assert(host_stub::contains("TIME ?"));
  assert(host_stub::contains("RECORDED IQ REPLAY"));
  assert(dashboard.handle_touch(100,681,hist,ui).kind==ActionKind::switch_view);
  assert(dashboard.handle_touch(120,270,hist,ui).kind==ActionKind::selected_history);
  for(int j=0;j<6;++j){
    ui.view=static_cast<View>(j);
    for (auto density:{Density::discovery_first,Density::analysis_first}) {
      ui.density=density;
      host_stub::reset();assert(dashboard.present(display,s,ui,true,false));
      assert(!host_stub::out_of_bounds && host_stub::full_clear_count==0);
      for(const auto& c:host_stub::calls) assert(c.y>=104); // shared header is never drawn by this module
    }
  }
  dashboard.leave();assert(!dashboard.active());
  std::puts("PASS 12 dashboard view/density combinations, evidence integrity, idle/overlay and geometry (recording stand-in only)");
}
