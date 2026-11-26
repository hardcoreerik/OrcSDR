#include "digital_voice_dashboard.hpp"
#include "digital_voice_dashboard_demo.hpp"
#ifdef NDEBUG
#undef NDEBUG // Conformance assertions are always active, including Release.
#endif
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
namespace dv=orcsdr::digital_voice_ui;
using namespace dv;
#define FCHK(c) do{if(!(c)){std::fprintf(stderr,"FLICKER TEST FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
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
  assert(dashboard.handle_touch(340,598,scan,ui).kind==ActionKind::skip_frequency);
  assert(dashboard.handle_touch(430,598,scan,ui).kind==ActionKind::none);   // selected signal is only a candidate: SAVE is disabled and says why
  assert(action_state(ActionKind::save_channel,scan,ui).reason==Disabled::not_verified);
  auto pick=dashboard.handle_touch(865,256,scan,ui);
  assert(pick.kind==ActionKind::select_scan_hit && pick.value==0);
  assert(apply_ui_action(ui,pick,scan) && ui.selected_scan_hit==0);
  scan.scan_hits[1].has_frequency=true;scan.scan_hits[1].frequency_hz=453775000;ui.selected_scan_hit=1;   // verified and with a frequency: SAVE becomes usable
  assert(dashboard.handle_touch(430,598,scan,ui).kind==ActionKind::save_channel);
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
  // Diff painting: only what changed is drawn. The labels that did not change are not drawn again and no card is refilled.
  assert(host_stub::contains("UNKNOWN"));  // the values that changed are drawn
  assert(host_stub::calls.size()<80);        // a few values, not the page
  for(const auto& c:host_stub::calls) assert(!(c.kind==host_stub::Prim::round_fill&&c.w>=400));
  assert(!host_stub::contains("3024109"));
  assert(host_stub::full_clear_count==0); // value update not screen clear
  {
    // FLICKER REGRESSION (docs/UI_RENDERING_STANDARD.md): a value that keeps changing (a heard counter) repaints only its own text. The
    // displayed value updates, no card or panel is refilled, nothing clears the screen, and the cost of a repaint stays small.
    Dashboard fd;lgfx::LovyanGFX fdisp;UiState fu{};fu.view=View::systems;
    Snapshot fs{};fs.revision=1;fs.systems_available=true;fs.last_heard_available=true;fs.observed_system_count=1;fs.db_available=true;fs.db_systems=1;
    fs.receiver=Receiver::receiving;
    auto& sys=fs.observed_systems[0];sys.protocol=Protocol::dmr;sys.evidence=Evidence::integrity_valid;sys.frequency_hz=152157500;sys.has_frequency=true;
    sys.colour_code=13;sys.colour_code_valid=true;sys.heard_count=100;
    host_stub::reset();FCHK(fd.present(fdisp,fs,fu,true,false));
    FCHK(host_stub::contains("HEARD 100"));
    for(uint32_t n=101;n<=110;++n) {
      sys.heard_count=n;++fs.revision;
      host_stub::reset();FCHK(fd.present(fdisp,fs,fu,true,false));
      char want[24];std::snprintf(want,sizeof(want),"HEARD %lu",static_cast<unsigned long>(n));
      FCHK(host_stub::contains(want));
      FCHK(host_stub::full_clear_count==0);
      for(const auto& c:host_stub::calls) FCHK(!((c.kind==host_stub::Prim::round_fill||c.kind==host_stub::Prim::round_line)&&c.w>=100));
      FCHK(host_stub::calls.size()<=12);
    }
    // The same value again, or a change on a page that does not show it, draws nothing.
    host_stub::reset();++fs.revision;FCHK(fd.present(fdisp,fs,fu,true,false));FCHK(host_stub::calls.empty());
  }
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
  // Systems paging: more systems in the database than rows on screen are reached with NEXT / PREV, in pages of four.
  {
    const auto hero=layout::kBasicHero;
    auto many_sys=nx;many_sys.revision=130;many_sys.db_systems=11;many_sys.observed_system_count=4;
    for(int i=1;i<4;++i)many_sys.observed_systems[i]=many_sys.observed_systems[0];
    UiState sui=ui;sui.view=View::systems;sui.system_offset=0;
    host_stub::reset();assert(dashboard.present(display,many_sys,sui,true,false));
    assert(host_stub::contains("NEXT") && host_stub::contains("1-4 OF 11"));
    assert(dashboard.handle_touch(hero.x+30,hero.y+400,many_sys,sui).kind==ActionKind::none);   // PREV is off on the first page
    const Action next=dashboard.handle_touch(hero.x+140,hero.y+400,many_sys,sui);
    assert(next.kind==ActionKind::page_systems && next.value==1);
    assert(apply_ui_action(sui,next,many_sys) && sui.system_offset==4 && sui.selected_system==0);
    many_sys.revision=131;many_sys.observed_system_count=4;
    host_stub::reset();assert(dashboard.present(display,many_sys,sui,true,false));
    assert(host_stub::contains("5-8 OF 11") && host_stub::contains("PREV"));
    assert(apply_ui_action(sui,next,many_sys) && sui.system_offset==8);                           // last page holds 3
    assert(!apply_ui_action(sui,next,many_sys) && sui.system_offset==8);                          // no page past the end
    assert(apply_ui_action(sui,{ActionKind::page_systems,0},many_sys) && sui.system_offset==4);
    assert(apply_ui_action(sui,{ActionKind::page_systems,0},many_sys) && sui.system_offset==0);
    assert(!apply_ui_action(sui,{ActionKind::page_systems,0},many_sys));
    assert(host_stub::full_clear_count==0);
    dashboard.leave();
    ui.view=View::systems;host_stub::reset();assert(dashboard.present(display,nx,ui,true,false));   // back to the single-system case
  }
  // DISCOVER: opened from SCAN, swept bands shown, start/stop/save/back actions, no tab for it.
  {
    const auto hero=layout::kBasicHero;
    auto ds=nx;ds.revision=140;ds.band_mask=0x0003;ds.band_channels=2240;ds.band_minutes=48;ds.discover_can_start=true;
    UiState dui=ui;dui.view=View::explore;dui.density=Density::discovery_first;
    host_stub::reset();dashboard.leave();assert(dashboard.present(display,ds,dui,true,false));
    const Action open=dashboard.handle_touch(hero.x+700,hero.y+440,ds,dui);
    assert(open.kind==ActionKind::open_discover);
    assert(apply_ui_action(dui,open,ds) && dui.view==View::discover);
    ds.revision=141;
    host_stub::reset();assert(dashboard.present(display,ds,dui,true,false));
    assert(host_stub::contains("DISCOVER SYSTEMS") && host_stub::contains("[X] 2M 144-148") && host_stub::contains("START DISCOVERY"));
    assert(host_stub::contains("2240 CH"));
    assert(dashboard.handle_touch(hero.x+100,hero.y+440,ds,dui).kind==ActionKind::discover_start);
    assert(dashboard.handle_touch(hero.x+330,hero.y+440,ds,dui).kind==ActionKind::none);            // SAVE TO LIST is off until something is found
    ds.discover_running=true;ds.discover_found=0;ds.revision=142;
    host_stub::reset();assert(dashboard.present(display,ds,dui,true,false));
    assert(host_stub::contains("STOP DISCOVERY") && host_stub::contains("DISCOVERING"));
    assert(dashboard.handle_touch(hero.x+100,hero.y+440,ds,dui).kind==ActionKind::discover_stop);
    ds.discover_running=false;ds.discover_found=2;ds.revision=143;
    host_stub::reset();assert(dashboard.present(display,ds,dui,true,false));
    assert(host_stub::contains("FOUND 2"));
    assert(dashboard.handle_touch(hero.x+330,hero.y+440,ds,dui).kind==ActionKind::none);            // storage hook missing: stays off
    ds.can_save_channel=true;ds.revision=144;
    host_stub::reset();assert(dashboard.present(display,ds,dui,true,false));
    assert(dashboard.handle_touch(hero.x+330,hero.y+440,ds,dui).kind==ActionKind::discover_save);
    ds.band_mask=0;ds.discover_can_start=false;ds.revision=145;
    host_stub::reset();assert(dashboard.present(display,ds,dui,true,false));
    assert(host_stub::contains("NO BANDS PICKED") && dashboard.handle_touch(hero.x+100,hero.y+440,ds,dui).kind==ActionKind::none);
    const Action back=dashboard.handle_touch(hero.x+500,hero.y+440,ds,dui);
    assert(back.kind==ActionKind::close_discover && apply_ui_action(dui,back,ds) && dui.view==View::explore);
    assert(host_stub::full_clear_count==0);
    dashboard.leave();
  }
  // Toggle density through SETUP page, purely UI-local.
  ui.view=View::setup;
  host_stub::reset();assert(dashboard.present(display,nx,ui,true,false));
  auto density=dashboard.handle_touch(110,555,nx,ui);
  assert(density.kind==ActionKind::toggle_density);
  assert(apply_ui_action(ui,density,nx) && ui.density==Density::analysis_first);
  host_stub::reset();assert(dashboard.present(display,nx,ui,true,false));
  assert(host_stub::contains("DENSITY: ANALYSIS"));
  assert(!apply_ui_action(ui,{ActionKind::scan,0},nx)); // no receiver side effects
  {
    // SETUP > MONITOR: the filter button shows the filter and how many channels it gives, and is a receiver-plan action (never a UI-local change).
    const auto adv=layout::kAdvancedMain;
    Snapshot ms=nx;ms.can_scan=true;ms.monitor_proto=2;ms.monitor_entries=9;ms.monitor_selected=true;ms.revision=nx.revision+1;                       // present() draws nothing for an unchanged revision; the app bumps it when these fields change
    host_stub::reset();assert(dashboard.present(display,ms,ui,true,false));
    assert(host_stub::contains("MONITOR: NXDN48 (9)"));
    assert(dashboard.handle_touch(adv.x+100,adv.y+330,ms,ui).kind==ActionKind::monitor_filter);
    assert(!apply_ui_action(ui,{ActionKind::monitor_filter,0},ms));
    ms.can_scan=false;ms.revision+=1;
    assert(dashboard.handle_touch(adv.x+100,adv.y+330,ms,ui).kind==ActionKind::none);
    host_stub::reset();assert(dashboard.present(display,ms,ui,true,false));
    assert(host_stub::contains("MONITOR: NOT CONNECTED"));
  }
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
