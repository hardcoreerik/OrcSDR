#define ORCSDR_TEST_REAL_TEXT_EDITOR
#include "settings_clock_recording_fixture.cpp"

extern "C" __declspec(dllexport) int test_connectivity_geometry() {
  using namespace orcsdr::settings;
  for(const Rect view: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    const auto settings=make_settings_layout(view);
    const auto c=make_connectivity_layout(settings.content);
    auto check=[&](Rect r) {return orcsdr::audio_header::header_inside(settings.content,r) && r.w>=44 && r.h>=44;};
    for(auto r:{c.scan,c.hidden,c.power,c.boot,c.antenna}) if(!check(r)) return 1;
    for(const auto& row:c.saved) for(auto r:{row.connect,row.up,row.down,row.forget}) if(!check(r)) return 2;
    for(auto r:c.networks) if(!check(r)) return 3;
  }
  return 0;
}
extern "C" __declspec(dllexport) int test_connectivity_actions_and_redraw() {
  using namespace orcsdr::settings;
  for(const Rect view: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    g_layout=make_settings_layout(view);State state{};state.wifi_power_enabled=true;
    state.saved_network_count=2;state.network_count=1;
    strlcpy(state.profiles[0].ssid,"First",sizeof(state.profiles[0].ssid));
    strlcpy(state.profiles[1].ssid,"Second",sizeof(state.profiles[1].ssid));
    strlcpy(state.networks[0].ssid,"Open test",sizeof(state.networks[0].ssid));
    enter(state,Section::connectivity);page_index()=0;draw();
    const auto c=make_connectivity_layout(g_layout.content);
    Action a;tap(c.scan,&a);if(a.kind!=ActionKind::scan_wifi) return 1;
    tap(c.boot,&a);if(a.kind!=ActionKind::wifi_start_at_boot_changed) return 2;
    tap(g_layout.next_page);if(page_index()!=1) return 3;
    tap(c.saved[1].up,&a);if(a.kind!=ActionKind::move_wifi_up || a.value!=1) return 4;
    tap(c.saved[0].connect,&a);if(a.kind!=ActionKind::connect_saved_wifi || a.value!=0) return 5;
    if(page_index()!=0 || !g_state.wifi_connecting) return 14;
    page_index()=1;draw_content();
    tap(c.saved[1].forget,&a);if(a.kind!=ActionKind::forget_wifi || a.value!=1) return 6;
    tap(g_layout.next_page);if(page_index()!=2) return 7;
    tap(c.networks[0],&a);if(a.kind!=ActionKind::connect_wifi) return 8;
    if(page_index()!=0 || !g_state.wifi_connecting) return 15;
    char ssid[33]{},password[64]{};
    if(!take_wifi_credentials(ssid,sizeof(ssid),password,sizeof(password)) || strcmp(ssid,"Open test") || password[0]) return 9;
    state=g_state;page_index()=0;draw_content();M5.Display.clear_records();
    strlcpy(state.wifi_message,"Scanning now",sizeof(state.wifi_message));update(state);
    bool displayed=false;
    for(const auto& t:M5.Display.texts) if(t.text=="Scanning now") displayed=true;
    if(!displayed || M5.Display.fill_screen_calls) return 10;
    for(const auto& f:M5.Display.fills) {
      Rect r{f.x,f.y,f.w,f.h};
      if(!orcsdr::audio_header::header_inside(c.status,r) && !orcsdr::audio_header::header_inside(c.message,r)) return 11;
    }
    state.wifi_power_enabled=false;update(state);tap(c.scan,&a);if(a.kind!=ActionKind::none) return 12;
    state.wifi_hosted_update_required=true;update(state);tap(c.power,&a);if(a.kind!=ActionKind::none) return 13;
  }
  return 0;
}

extern "C" __declspec(dllexport) int test_connectivity_real_editor() {
  using namespace orcsdr::settings;
  namespace editor=orcsdr::text_editor;
  for(const Rect view: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    g_layout=make_settings_layout(view);State state{};state.wifi_power_enabled=true;
    enter(state,Section::connectivity);page_index()=0;draw();
    const auto c=make_connectivity_layout(g_layout.content);tap(c.hidden);
    if(!editor::active() || g_wifi_edit!=WifiEdit::ssid || editor::masked()) return 1;
    const auto v=editor::layout().viewport;
    if(v.x!=view.x || v.w!=view.w || v.y<g_layout.header.y+g_layout.header.h || v.y+v.h>view.y+view.h) return 2;
    const auto generation=editor::generation();
    auto key=editor::layout().keys[1][0];handle_touch(key.x+key.w/2,key.y+key.h/2);
    if(strcmp(editor::value(),"q")) return 3;
    int x,y;editor::accept_point(&x,&y);handle_touch(x,y);
    if(g_wifi_edit!=WifiEdit::password || !editor::masked() || editor::generation()<=generation) return 4;
    key=editor::layout().keys[1][0];handle_touch(key.x+key.w/2,key.y+key.h/2);
    if(strcmp(editor::value(),"q")) return 5;
    editor::accept_point(&x,&y);const auto a=handle_touch(x,y);
    if(a.kind!=ActionKind::connect_wifi || editor::active() || g_wifi_edit!=WifiEdit::none) return 6;
    if(page_index()!=0 || !g_state.wifi_connecting) return 8;
    char ssid[33],password[64];if(!take_wifi_credentials(ssid,sizeof(ssid),password,sizeof(password)) || strcmp(ssid,"q") || strcmp(password,"q")) return 7;
    tap(c.hidden);editor::cancel_point(&x,&y);handle_touch(x,y);
    if(editor::active() || geometry_locked()) return 8;
  }
  return 0;
}

extern "C" __declspec(dllexport) int test_all_settings_sections_fit() {
  using namespace orcsdr::settings;
  for(const Rect view: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    M5.Display.set_size(view.w+144,view.h+144);
    g_layout=make_settings_layout(view);State state{};
    for(int section=0;section<int(Section::count);++section) {
      enter(state,Section(section));
      for(size_t page=0;page<page_count();++page) {
        page_index()=page;M5.Display.clear_records();draw();
        if(M5.Display.fill_screen_calls) return 1+section;
        for(const auto& f:M5.Display.fills)
          if(!orcsdr::audio_header::header_inside(view,{f.x,f.y,f.w,f.h})) return 20+section;
        for(const auto& t:M5.Display.texts)
          if(t.x<view.x || t.y<view.y || t.x>=view.x+view.w || t.y>=view.y+view.h) return 40+section;
      }
    }
  }
  return 0;
}
extern "C" __declspec(dllexport) int test_sparse_catalog_identity_and_coordinate() {
  using namespace orcsdr::settings;namespace editor=orcsdr::text_editor;
  g_layout=make_settings_layout({72,72,424,1088});State state{};state.catalog_ready=true;
  strlcpy(state.catalog_packs[2].id,"first",20);strlcpy(state.catalog_packs[9].id,"second",20);
  state.catalog_packs[9].installed=true;
  enter(state,Section::data_maps);page_index()=2;draw_content();
  const SettingsFormLayout l{g_layout.content};Action a;
  tap(l.half(144),&a);if(a.kind!=ActionKind::catalog_install || a.value!=9) return 1;
  tap(l.half(144,true),&a);if(a.kind!=ActionKind::none || g_catalog_cursor.armed!=9) return 2;
  tap(l.half(144,true),&a);if(a.kind!=ActionKind::catalog_remove || a.value!=9) return 3;
  enter(state,Section::location_adsb);page_index()=0;
  tap(l.coordinate(false));if(g_edit!=EditField::latitude || !editor::active()) return 4;
  editor::begin("LATITUDE","91",19,false,"SAVE");int x,y;editor::accept_point(&x,&y);handle_touch(x,y);
  if(g_edit==EditField::none || !editor::active()) return 5;
  editor::begin("LATITUDE","45.5",19,false,"SAVE");editor::accept_point(&x,&y);a=handle_touch(x,y);
  if(a.kind!=ActionKind::location_changed || g_state.latitude_e7!=455000000 || geometry_locked()) return 6;
  return 0;
}

extern "C" __declspec(dllexport) int test_readable_wrapping_and_full_ssids() {
  using namespace orcsdr::settings;
  for(const Rect view: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    g_layout=make_settings_layout(view);State state{};
    state.saved_network_count=1;state.network_count=1;
    const char* ssid="12345678901234567890123456789012";
    strlcpy(state.profiles[0].ssid,ssid,sizeof(state.profiles[0].ssid));
    strlcpy(state.networks[0].ssid,ssid,sizeof(state.networks[0].ssid));
    enter(state,Section::connectivity);
    for(unsigned page: {1u,2u}) {
      page_index()=page;M5.Display.clear_records();draw_content();bool seen=false;
      for(const auto& t:M5.Display.texts) if(t.text==ssid) {
        seen=true;if(t.size<2 || t.x+int(t.text.size())*6*t.size>view.x+view.w) return 1;
      }
      if(!seen) return 2;
    }
    M5.Display.clear_records();const Rect r{view.x+8,view.y+200,view.w-16,192};
    const char* words="Hold physical C6 BOOT, then tap Update now. Keep BOOT held until connected.";
    const int used=wrapped_text(words,r,TFT_WHITE);
    if(used>r.h || M5.Display.texts.empty()) return 3;
    for(const auto& t:M5.Display.texts)
      if(t.size!=3 || t.x+int(t.text.size())*6*t.size>r.x+r.w || t.y+12>r.y+r.h) return 4;
  }
  return 0;
}

extern "C" __declspec(dllexport) int test_storage_status_is_real_and_bounded() {
  using namespace orcsdr::settings;
  for(const Rect view: {Rect{72,72,424,1088},Rect{72,72,1088,424}}) {
    g_layout=make_settings_layout(view);State s{};enter(s,Section::storage);
    M5.Display.clear_records();s.sd_ready=true;s.sd_total_bytes=32ull*1024*1024*1024;
    s.sd_capacity_known=false;update(s);
    if(M5.Display.fill_screen_calls || M5.Display.fills.size()!=2) return 1;
    bool ready=false,unknown=false;
    for(const auto& t:M5.Display.texts) {ready|=t.text=="READY";unknown|=t.text.find("FREE UNKNOWN")!=std::string::npos;}
    if(!ready || !unknown) return 2;
    M5.Display.clear_records();s.sd_capacity_known=true;s.sd_free_bytes=20ull*1024*1024*1024;update(s);
    bool free=false;for(const auto& t:M5.Display.texts) free|=t.text.find("20.0 / 32.0")!=std::string::npos;
    if(!free || M5.Display.fills.size()!=2 || M5.Display.fill_screen_calls) return 3;
  }
  return 0;
}
