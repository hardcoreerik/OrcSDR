#include "digital_voice_dashboard.hpp"
#include "digital_voice_dashboard_demo.hpp"
#ifdef NDEBUG
#undef NDEBUG // Conformance assertions are always active, including Release.
#endif
#include <cassert>
#include <cstdio>
#include <cstring>
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
  assert(host_stub::contains("SCANNER NOT CONNECTED"));
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
  scan.scan.mode=ScanMode::channel_list;scan.scan.stage=ScanStage::scanning;scan.scan.channel_index=3;
  std::strcpy(scan.scan.plan_name,"SAVED LIST");
  host_stub::reset();assert(dashboard.present(display,scan,ui,true,false));
  assert(host_stub::contains("SCANNING"));
  assert(host_stub::contains("CHANNEL LIST  SAVED LIST  CH 3 / 12"));
  assert(dashboard.handle_touch(55,598,scan,ui).kind==ActionKind::scan);
  assert(dashboard.handle_touch(240,598,scan,ui).kind==ActionKind::none);
  scan.can_hold=true;
  scan.can_skip=true;
  scan.can_save_channel=true;
  scan.scan_hit_count=2;
  scan.scan_hits[0].protocol=Protocol::nxdn48;
  scan.scan_hits[0].evidence=Evidence::candidate;
  scan.scan_hits[0].frequency_hz=447100000;
  scan.scan_hits[0].has_frequency=true;
  scan.scan_hits[1].protocol=Protocol::nxdn96;
  scan.scan_hits[1].evidence=Evidence::integrity_valid;
  scan.scan_hits[1].has_frequency=false;
  scan.revision=8;
  host_stub::reset();assert(dashboard.present(display,scan,ui,true,false));
  assert(host_stub::contains("NXDN48") && host_stub::contains("NXDN96"));
  assert(host_stub::contains("447.100000 MHz"));
  assert(host_stub::contains("FREQUENCY UNKNOWN"));
  assert(dashboard.handle_touch(240,598,scan,ui).kind==ActionKind::toggle_hold);
  assert(dashboard.handle_touch(380,598,scan,ui).kind==ActionKind::skip_frequency);
  assert(dashboard.handle_touch(496,598,scan,ui).kind==ActionKind::none);   // selected signal is only a candidate: SAVE is disabled and says why
  assert(action_state(ActionKind::save_channel,scan,ui).reason==Disabled::not_verified);
  auto pick=dashboard.handle_touch(865,256,scan,ui);
  assert(pick.kind==ActionKind::select_scan_hit && pick.value==0);
  assert(apply_ui_action(ui,pick,scan) && ui.selected_scan_hit==0);
  scan.scan_hits[1].has_frequency=true;scan.scan_hits[1].frequency_hz=453775000;ui.selected_scan_hit=1;   // verified and with a frequency: SAVE becomes usable
  assert(dashboard.handle_touch(496,598,scan,ui).kind==ActionKind::save_channel);
  ui.selected_scan_hit=0;
  ui.view=View::live;
  host_stub::reset();
  assert(dashboard.present(display,s,ui,true,false));
  assert(host_stub::contains("3024109"));
  assert(host_stub::contains("99"));
  assert(host_stub::contains("COLOR CODE"));
  assert(host_stub::contains("TIME SLOT"));
  assert(host_stub::contains("UNKNOWN"));
  assert(!host_stub::contains("TS1") && !host_stub::contains("TS2"));
  assert(host_stub::contains("IQ METADATA / RF UNVERIFIED"));
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
  assert(host_stub::contains("3024109>99"));   // identity-validated rows show the decoded source>destination, never a generic label
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
  // NXDN field-level evidence gates: untrusted candidate cannot expose RAN/site/identity.
  ui.view=View::live;ui.density=Density::discovery_first;
  auto nx=demo::make(demo::Case::nxdn_planned);
  nx.revision=11;nx.protocol=Protocol::nxdn96;nx.ran_valid=true;nx.ran=0;
  nx.nxdn.channel_valid=true;nx.nxdn.channel=NxdnChannel::rcch;
  nx.nxdn.topology_valid=true;nx.nxdn.topology=NxdnTopology::type_c;
  nx.nxdn.channel_number_valid=true;nx.nxdn.channel_number=507;
  nx.evidence=Evidence::candidate;
  host_stub::reset();assert(dashboard.present(display,nx,ui,true,false));
  assert(host_stub::contains("NXDN96") && host_stub::contains("RAN"));
  assert(!host_stub::contains("TYPE-C TRUNK") && !host_stub::contains("RCCH / CONTROL"));
  assert(!host_stub::contains("507"));
  nx.evidence=Evidence::integrity_valid;nx.revision=12;
  host_stub::reset();assert(dashboard.present(display,nx,ui,true,false));
  assert(host_stub::contains("TYPE-C TRUNK") && host_stub::contains("RCCH / CONTROL"));
  assert(host_stub::contains("507") && host_stub::contains("0")); // RAN 0 is a valid field
  assert(!host_stub::contains("CLEAR AUDIO AVAILABLE"));
  // Systems page receives verified list entries and never fabricates directory names.
  ui.view=View::systems;nx.revision=13;nx.systems_available=true;
  nx.observed_system_count=1;nx.observed_systems[0].protocol=Protocol::nxdn96;
  nx.observed_systems[0].topology=NxdnTopology::type_c;
  nx.observed_systems[0].topology_valid=true;
  nx.observed_systems[0].evidence=Evidence::integrity_valid;
  nx.observed_systems[0].ran_valid=true;nx.observed_systems[0].ran=0;
  nx.observed_systems[0].frequency_hz=453775000;nx.observed_systems[0].has_frequency=true;
  host_stub::reset();assert(dashboard.present(display,nx,ui,true,false));
  assert(host_stub::contains("TYPE-C TRUNK") && host_stub::contains("453.775000 MHz"));
  assert(dashboard.handle_touch(100,263,nx,ui).kind==ActionKind::select_system);
  // Toggle density through SETUP page, purely UI-local.
  ui.view=View::setup;
  host_stub::reset();assert(dashboard.present(display,nx,ui,true,false));
  auto density=dashboard.handle_touch(110,555,nx,ui);
  assert(density.kind==ActionKind::toggle_density);
  assert(apply_ui_action(ui,density,nx) && ui.density==Density::analysis_first);
  host_stub::reset();assert(dashboard.present(display,nx,ui,true,false));
  assert(host_stub::contains("DENSITY: ANALYSIS"));
  assert(!apply_ui_action(ui,{ActionKind::scan,0},nx)); // no receiver side effects
  // All 8 history events are accessible by paging; unknown timestamps remain unknown.
  ui.view=View::last_heard;ui.density=Density::discovery_first;
  auto many=demo::make(demo::Case::history_demo);many.revision=14;
  many.history_count=8;
  for(int i=2;i<8;++i)many.history[i]=many.history[0];
  host_stub::reset();assert(dashboard.present(display,many,ui,true,false));
  assert(host_stub::contains("NEXT"));
  auto page=dashboard.handle_touch(180,594,many,ui);
  assert(page.kind==ActionKind::page_history && page.value==1);
  assert(apply_ui_action(ui,page,many) && ui.history_offset==2);
  host_stub::reset();assert(dashboard.present(display,many,ui,true,false));
  assert(host_stub::contains("PREV"));
  dashboard.leave();assert(!dashboard.active());
  std::puts("PASS 12 dashboard view/density combinations, evidence integrity, idle/overlay and geometry (recording stand-in only)");
}
