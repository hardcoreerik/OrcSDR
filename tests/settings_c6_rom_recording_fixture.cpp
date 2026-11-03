#include "settings_clock_recording_fixture.cpp"

namespace {
using namespace orcsdr::settings;
namespace ui=orcsdr::c6_recovery_ui;
State rom_state() {
  State s{};s.wifi_c6_rom_recovery=true;s.wifi_c6_image_embedded=true;
  strlcpy(s.wifi_c6_rom_target_version,"3.0.6",sizeof(s.wifi_c6_rom_target_version));
  strlcpy(s.wifi_hosted_c6_version,"2.12.9",sizeof(s.wifi_hosted_c6_version));return s;
}
}
extern "C" __declspec(dllexport) int test_rom_workflow_actions_and_locks() {
  using namespace orcsdr::settings;
  for(const Rect viewport: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    g_layout=make_settings_layout(viewport);auto s=rom_state();enter(s,Section::firmware_updates);
    Action a;tap(g_layout.firmware_action(),&a);
    if(a.kind!=ActionKind::c6_rom_begin || !geometry_locked()) return 1;
    s.wifi_c6_rom_busy=true;s.wifi_c6_rom_phase=ui::Phase::connecting;update(s);
    tap(g_layout.next_section,&a);if(section()!=Section::firmware_updates || a.kind!=ActionKind::none) return 2;
    s.wifi_c6_rom_phase=ui::Phase::release_boot;update(s);
    tap(g_layout.firmware_action(true),&a);if(a.kind!=ActionKind::c6_rom_release_install) return 3;
    tap(g_layout.firmware_action(true),&a);if(a.kind!=ActionKind::none) return 4;
    tap(g_layout.firmware_cancel(),&a);if(a.kind!=ActionKind::none) return 8;
    s.wifi_c6_rom_phase=ui::Phase::connecting;update(s);
    tap(g_layout.firmware_action(),&a);if(a.kind!=ActionKind::c6_rom_cancel) return 10;
    tap(g_layout.firmware_action(),&a);if(a.kind!=ActionKind::none) return 11;
    s.wifi_c6_rom_phase=ui::Phase::release_boot_failure;update(s);
    tap(g_layout.firmware_action(),&a);if(a.kind!=ActionKind::c6_rom_release_resume) return 5;
    tap(g_layout.firmware_action(),&a);if(a.kind!=ActionKind::none) return 9;
    s.wifi_c6_rom_phase=ui::Phase::writing;update(s);
    tap(g_layout.firmware_action(),&a);if(a.kind!=ActionKind::none) return 6;
    s.wifi_c6_rom_phase=ui::Phase::complete;s.wifi_c6_rom_busy=false;update(s);
    if(geometry_locked()) return 7;
  }
  return 0;
}
extern "C" __declspec(dllexport) int test_rom_progress_is_bounded_and_visible() {
  using namespace orcsdr::settings;
  for(const Rect viewport: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    g_layout=make_settings_layout(viewport);auto s=rom_state();s.wifi_c6_rom_busy=true;s.wifi_c6_rom_phase=ui::Phase::writing;
    enter(s,Section::firmware_updates);
    for(unsigned percent: {10u,20u,30u}) {
      M5.Display.clear_records();s.wifi_c6_update_percent=percent;update(s);
      if(M5.Display.fill_screen_calls || M5.Display.fills.size()!=1) return 1;
      const auto fill=M5.Display.fills[0];
      if(fill.x!=g_layout.content.x+8 || fill.y!=g_layout.content.y+40 || fill.h!=48) return 2;
      bool seen=false;const auto value=std::to_string(percent)+"%";
      for(const auto& t:M5.Display.texts) if(t.text.find(value)!=std::string::npos) seen=true;
      if(!seen) return 3;
    }
    M5.Display.clear_records();update(s);if(!M5.Display.fills.empty()) return 4;
    s.wifi_c6_rom_busy=false;s.wifi_c6_rom_phase=ui::Phase::idle;update(s);
  }
  return 0;
}
extern "C" __declspec(dllexport) int test_hosted_method_unchanged_and_missing_image_rejects() {
  using namespace orcsdr::settings;
  for(const Rect viewport: {Rect{0,0,1280,720},Rect{72,72,424,1088}}) {
    g_layout=make_settings_layout(viewport);State hosted{};hosted.wifi_c6_image_embedded=true;
    strlcpy(hosted.wifi_c6_update_state,"ready",sizeof(hosted.wifi_c6_update_state));
    enter(hosted,Section::firmware_updates);Action a;
    if(g_layout.legacy) a=handle_touch(400,470);
    else {page_index()=1;draw_content();tap(g_layout.firmware_action(),&a);}
    if(a.kind!=ActionKind::c6_update_confirm) return 1;
  }
  g_layout=make_settings_layout({72,72,424,1088});auto s=rom_state();s.wifi_c6_image_embedded=false;
  enter(s,Section::firmware_updates);Action a;tap(g_layout.firmware_action(),&a);
  return a.kind==ActionKind::none?0:2;
}

extern "C" __declspec(dllexport) int test_completed_and_current_hide_update() {
  using namespace orcsdr::settings;
  for(const Rect viewport: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    g_layout=make_settings_layout(viewport);auto s=rom_state();
    s.wifi_c6_rom_phase=ui::Phase::complete;s.wifi_hosted_transport_ready=true;
    strlcpy(s.wifi_hosted_c6_version,"3.9.7",sizeof(s.wifi_hosted_c6_version));
    strlcpy(s.wifi_c6_rom_target_version,"3.9.7",sizeof(s.wifi_c6_rom_target_version));
    auto unknown=s;unknown.wifi_c6_rom_phase=ui::Phase::idle;
    unknown.wifi_c6_rom_target_version[0]='\0';
    if(rom_firmware_current(unknown)) return 6;
    strlcpy(unknown.wifi_c6_rom_target_version,"3.0.6",sizeof(unknown.wifi_c6_rom_target_version));
    if(rom_firmware_current(unknown)) return 7;
    for(auto phase:{ui::Phase::complete,ui::Phase::idle}) {
      s.wifi_c6_rom_phase=phase;M5.Display.clear_records();enter(s,Section::firmware_updates);
      bool heading=false,detail=false;
      for(const auto& t:M5.Display.texts) {
        if(t.text.find("UPDATE NOW")!=std::string::npos || t.text.find("Hold physical")!=std::string::npos) return 1;
        heading|=t.text.find(phase==ui::Phase::complete?"C6 UPDATE COMPLETE":"C6 UP TO DATE")!=std::string::npos;
        detail|=t.text.find(phase==ui::Phase::complete?"Installed and verified":"No update needed")!=std::string::npos;
      }
      if(!heading || !detail) return 2;
      const auto action_rect=g_layout.firmware_action();
      for(const auto& f:M5.Display.fills)
        if(f.x==action_rect.x && f.y==action_rect.y && f.w==action_rect.w && f.h==action_rect.h) return 5;
      Action a;tap(g_layout.firmware_action(),&a);if(a.kind!=ActionKind::none) return 3;
    }
    // A matching but unreachable C6 still needs its recovery path.
    s.wifi_hosted_transport_ready=false;update(s);Action a;tap(g_layout.firmware_action(),&a);
    if(a.kind!=ActionKind::c6_rom_begin) return 4;
  }
  return 0;
}
