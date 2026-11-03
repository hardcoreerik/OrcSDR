#include <M5Unified.h>
#include <esp_attr.h>
#include "../../apps/orcsdr-tab5/ui/shortwave_dashboard.cpp"
#include <cassert>
#include <cstring>

namespace orcsdr::audio_header {
static HeaderLayout g_header{};
bool set_viewport(HeaderRect r){g_header=make_header_layout(r);if(g_header.valid)focus_nav::set_viewport(r.x+r.w,r.y+r.h);return g_header.valid;}
const HeaderLayout& layout(){return g_header;}
void draw_brand(const char* s){M5.Display.drawString(s,0,0);}
void draw_home_button(){} void draw_battery(int32_t){}
void draw_mute_button(bool){} void draw_visualizer_button(bool){}
void draw_settings_button(){}
bool home_hit(int32_t,int32_t){return false;}
bool settings_hit(int32_t,int32_t){return false;}
bool self_check(){return true;}
}
namespace orcsdr::freq_keypad {
static Layout g_keypad{};
void draw(int,uint16_t,const char*,const char*,const char*,const char*,const Rect* r){if(r)g_keypad=make_layout(*r);}
const Layout& layout(){return g_keypad;}
Result handle_touch(int32_t,int32_t,char*,size_t){return Result::none;}
bool key_point(char,int*,int*){return false;}
void cancel_point(int*,int*){} void tune_point(int*,int*){}
bool self_check(){return true;}
}
namespace orcsdr::text_editor {
static Layout g_editor{};static bool g_active=false;static uint64_t g_generation=0;static char g_value[64]{};
void begin(const char*,const char* initial,size_t maximum,bool,const char*){g_active=true;++g_generation;std::snprintf(g_value,sizeof(g_value),"%.*s",static_cast<int>(std::min(maximum,sizeof(g_value)-1)),initial?initial:"");}
bool set_viewport(Rect r){g_editor=make_layout(r);return g_editor.valid;}
const Layout& layout(){return g_editor;} bool active(){return g_active;}
uint64_t generation(){return g_generation;}const char* value(){return g_value;}
void draw(){} Result handle_touch(int,int){return Result::none;}
keyboard_input::EditAction handle_key(const keyboard_input::Key&){return keyboard_input::EditAction::none;}
bool masked(){return false;}void accept_point(int*,int*){}void cancel_point(int*,int*){}
void close(){g_active=false;}bool self_check(){return true;}
}

using namespace orcsdr::shortwave;
static Action tap(layout::Rect r){assert(r.w>0&&r.h>0);return handle_touch(r.x+r.w/2,r.y+r.h/2);}
template<size_t N>static void max_text(char(&out)[N],char fill,const char* suffix){const size_t n=std::strlen(suffix);assert(n<N);std::memset(out,fill,N-1);std::memcpy(out+N-1-n,suffix,n);out[N-1]='\0';}
static void check_view(layout::Rect view){
  assert(orcsdr::shortwave::set_viewport(view));
  MemoryTable memories;LogTable logs;
  for(int i=0;i<24;++i){Memory m{};m.frequency_hz=5000000+uint32_t(i)*1000;m.bandwidth_hz=6000;std::strcpy(m.mode,"AM");if(i==23){max_text(m.station,'M',"FINAL_MEMORY_23");max_text(m.notes,'N',"FINAL_MEMORY_NOTES_23");}else{std::snprintf(m.station,sizeof(m.station),"Memory record %02d",i);std::snprintf(m.notes,sizeof(m.notes),"Preserved notes for record %02d",i);}assert(memories.upsert(m)==RecordResult::ok);}
  for(int i=0;i<24;++i){LogEntry e{};e.timestamp_utc=uint64_t(i+1);e.frequency_hz=5000000+uint32_t(i)*1000;e.bandwidth_hz=6000;e.signal_dbfs=-12.5f;std::strcpy(e.mode,"AM");if(i==23){max_text(e.station,'S',"FINAL_LOG_23");max_text(e.antenna,'A',"FINAL_ANTENNA_23");max_text(e.notes,'N',"FINAL_LOG_NOTES_23");}else{std::snprintf(e.station,sizeof(e.station),"Log record %02d",i);std::snprintf(e.antenna,sizeof(e.antenna),"Antenna %02d",i);std::snprintf(e.notes,sizeof(e.notes),"Long notes for record %02d",i);}assert(logs.append(e)==RecordResult::ok);}
  Snapshot s{};s.frequency_hz=7100000;s.relative_dbfs=-61.5f;s.span_hz=480000;s.filter_bandwidth_hz=6000;s.running=true;s.driver_ready=true;s.utc_valid=true;s.utc_minute=600;s.utc_weekday=1;s.memories=&memories;s.logs=&logs;s.storage_status=StorageStatus::ready;s.hunt_candidate_count=8;
  for(int i=0;i<8;++i){s.hunt_candidates[i].frequency_hz=7000000+uint32_t(i)*1000;s.hunt_candidates[i].level_dbfs=-30.0f;}
  enter(s);assert(active());
  for(int i=0;i<5;++i){const auto a=tap(g_layout.tab[i]);(void)a;assert(static_cast<int>(g_state.tab())==i);draw();}
  g_state.select_tab(Tab::on_air);draw();const int air_clears=lgfx::screen_clears;lgfx::fills.clear();lgfx::paints.clear();
  s.frequency_hz=5000000;update(s);assert(lgfx::screen_clears==air_clears);
  for(const auto& f:lgfx::fills)assert(!(f.x==g_layout.content.x&&f.y==g_layout.content.y&&f.w==g_layout.content.w&&f.h==g_layout.content.h));
  bool station_visible=false;for(const auto& p:lgfx::paints)station_visible|=p.text.find("WWV")!=std::string::npos;assert(station_visible);
  // All data records, including the final record, remain accessible through page navigation.
  g_state.select_tab(Tab::memory);draw();
  const size_t page_size=g_layout.legacy?7u:size_t(g_layout.row_count);
  const size_t page_count=(24+page_size-1)/page_size;
  for(size_t p=1;p<page_count;++p)tap(g_layout.legacy?layout::Rect{300,170,70,58}:g_layout.page_controls[2]);
  const size_t final_row=23-(page_count-1)*page_size;
  const auto final_mem= g_layout.legacy?layout::Rect{36,250+int(final_row)*50,780,42}:layout::Rect{g_layout.row_actions[final_row][0].x,g_layout.row_actions[final_row][0].y,g_layout.row_actions[final_row][0].w,g_layout.row_actions[final_row][0].h};
  assert(tap(final_mem).kind==ActionKind::tune_hz&&tap(final_mem).value==5023000);
  const auto mem_edit=g_layout.legacy?layout::Rect{936,250+int(final_row)*50,130,42}:layout::Rect{g_layout.row_actions[final_row][2].x,g_layout.row_actions[final_row][2].y,g_layout.row_actions[final_row][2].w,g_layout.row_actions[final_row][2].h};
  tap(mem_edit);assert(std::strlen(pending_memory_label())==sizeof(memories.at(23)->station)-1&&std::strlen(pending_memory_notes())==sizeof(memories.at(23)->notes)-1&&std::strstr(pending_memory_notes(),"FINAL_MEMORY_NOTES_23")!=nullptr);
  const auto old_page=g_memory_page;const auto rotated_memory=layout::Rect{view.x,view.y,view.h,view.w};
  draw();const uint64_t editor_generation=orcsdr::text_editor::generation();const char* editor_value=orcsdr::text_editor::value();char saved_editor_value[64]{};std::strncpy(saved_editor_value,editor_value,sizeof(saved_editor_value)-1);
  assert(orcsdr::shortwave::set_viewport(rotated_memory));draw();assert(g_state.tab()==Tab::memory&&g_memory_page==old_page&&g_edit_memory==23&&std::strstr(pending_memory_notes(),"FINAL_MEMORY_NOTES_23")!=nullptr);
  assert(orcsdr::text_editor::active()&&orcsdr::text_editor::generation()==editor_generation&&std::strcmp(orcsdr::text_editor::value(),saved_editor_value)==0&&orcsdr::text_editor::layout().viewport.w==rotated_memory.w);
  assert(orcsdr::shortwave::set_viewport(view));draw();g_state.close_modal();draw();
  g_state.select_tab(Tab::logbook);draw();
  const size_t log_pages=(24+page_size-1)/page_size;
  for(size_t p=1;p<log_pages;++p)tap(g_layout.legacy?layout::Rect{300,170,70,58}:g_layout.page_controls[2]);
  const size_t last=23-(log_pages-1)*page_size;
  const auto final_log=g_layout.legacy?layout::Rect{36,250+int(last)*50,880,42}:layout::Rect{g_layout.row_actions[last][0].x,g_layout.row_actions[last][0].y,g_layout.row_actions[last][0].w,g_layout.row_actions[last][0].h};
  assert(tap(final_log).kind==ActionKind::none); // row selection opens detail editor only on EDIT
  const auto log_edit=g_layout.legacy?layout::Rect{926,250+int(last)*50,140,42}:layout::Rect{g_layout.row_actions[last][2].x,g_layout.row_actions[last][2].y,g_layout.row_actions[last][2].w,g_layout.row_actions[last][2].h};
  tap(log_edit);assert(std::strlen(pending_log_antenna())==sizeof(logs.at(23)->antenna)-1&&std::strlen(pending_log_notes())==sizeof(logs.at(23)->notes)-1&&std::strstr(pending_log_notes(),"FINAL_LOG_NOTES_23")!=nullptr);g_state.close_modal();draw();
  s.storage_available=false;s.storage_status=StorageStatus::unavailable;update(s);g_state.select_tab(Tab::memory);draw();
  const auto unavailable_fav=g_layout.legacy?layout::Rect{826,250+int(final_row)*50,100,42}:g_layout.row_actions[final_row][1];
  const auto unavailable_edit=g_layout.legacy?layout::Rect{936,250+int(final_row)*50,130,42}:g_layout.row_actions[final_row][2];
  const auto unavailable_delete=g_layout.legacy?layout::Rect{1076,250+int(final_row)*50,168,42}:g_layout.row_actions[final_row][3];
  assert(tap(unavailable_fav).kind==ActionKind::none&&tap(unavailable_edit).kind==ActionKind::none&&tap(unavailable_delete).kind==ActionKind::none);
  for(size_t i=0;i<orcsdr::focus_nav::count();++i){orcsdr::focus_nav::Rect r{};assert(orcsdr::focus_nav::get(i,&r));assert(!(r.x==unavailable_fav.x&&r.y==unavailable_fav.y));assert(!(r.x==unavailable_edit.x&&r.y==unavailable_edit.y));assert(!(r.x==unavailable_delete.x&&r.y==unavailable_delete.y));}
  g_state.select_tab(Tab::logbook);draw();const auto unavailable_log_edit=g_layout.legacy?layout::Rect{926,250+int(last)*50,140,42}:g_layout.row_actions[last][2];const auto unavailable_log_delete=g_layout.legacy?layout::Rect{1076,250+int(last)*50,168,42}:g_layout.row_actions[last][3];
  assert(tap(unavailable_log_edit).kind==ActionKind::none&&tap(unavailable_log_delete).kind==ActionKind::none);
  s.storage_available=true;s.storage_status=StorageStatus::ready;update(s);
  g_state.select_tab(Tab::hunt);draw();
  const auto hunt_start=g_layout.legacy?layout::Rect{790,190,220,58}:layout::Rect{g_layout.page_controls[3].x,g_layout.page_controls[3].y,g_layout.page_controls[3].w,g_layout.page_controls[3].h};
  const auto hunt_cancel=g_layout.legacy?layout::Rect{1020,190,224,58}:layout::Rect{g_layout.page_controls[4].x,g_layout.page_controls[4].y,g_layout.page_controls[4].w,g_layout.page_controls[4].h};
  lgfx::paints.clear();assert(tap(hunt_start).kind==ActionKind::hunt_start);bool starting=false;for(const auto& p:lgfx::paints)starting|=p.text=="STARTING...";assert(starting);
  assert(tap(hunt_cancel).kind==ActionKind::hunt_cancel);
  while(g_hunt_page+1<(8+size_t(g_layout.hunt_row_count)-1)/size_t(g_layout.hunt_row_count))tap(g_layout.hunt_next);
  const size_t last_candidate=7-g_hunt_page*size_t(g_layout.hunt_row_count);
  const auto candidate=g_layout.legacy?layout::Rect{36,270+int(last_candidate)*52,1208,44}:layout::Rect{g_layout.rows[last_candidate].x,g_layout.rows[last_candidate].y,g_layout.rows[last_candidate].w,g_layout.rows[last_candidate].h};
  assert(tap(candidate).kind==ActionKind::tune_hz&&tap(candidate).value==7007000);
  const int hunt_clears=lgfx::screen_clears;lgfx::fills.clear();lgfx::paints.clear();
  s.hunt_active=true;s.hunt_total=99;for(int i=0;i<4;++i){s.hunt_step=uint16_t(i+1);update(s);}
  assert(lgfx::screen_clears==hunt_clears);for(const auto& f:lgfx::fills)assert(!(f.x==g_layout.content.x&&f.y==g_layout.content.y&&f.w==g_layout.content.w&&f.h==g_layout.content.h));
  bool latest_progress=false;for(const auto& p:lgfx::paints)latest_progress|=p.text=="4 / 99";assert(latest_progress);
  g_state.select_tab(Tab::live);draw();
  float levels[160];for(int i=0;i<160;++i)levels[i]=-90.0f+float(i%23);
  draw_spectrum(levels,0,160,-100.0f);const size_t trace_bytes=lgfx::live_sprite_bytes;
  assert(trace_bytes<=size_t(1230)*204*2&&sizeof(g_waterfall_row)==size_t(1232)*2);
  draw_spectrum(levels,0,160,-100.0f);assert(lgfx::live_sprite_bytes==trace_bytes);
  bool step_focus=false;for(size_t i=0;i<orcsdr::focus_nav::count();++i){orcsdr::focus_nav::Rect r{};assert(orcsdr::focus_nav::get(i,&r));step_focus|=r.x==g_layout.quick[0].x&&r.y==g_layout.quick[0].y&&r.w==g_layout.quick[0].w&&r.h==g_layout.quick[0].h;}assert(step_focus);
  if(g_layout.portrait){bool bottom_tab_focus=false;const auto bottom_tab=g_layout.tab[4];for(size_t i=0;i<orcsdr::focus_nav::count();++i){orcsdr::focus_nav::Rect r{};assert(orcsdr::focus_nav::get(i,&r));bottom_tab_focus|=r.x==bottom_tab.x&&r.y==bottom_tab.y&&r.w==bottom_tab.w&&r.h==bottom_tab.h;}assert(bottom_tab.y+bottom_tab.h>720&&bottom_tab_focus);}
  orcsdr::focus_nav::focus_at(g_layout.quick[0].x+4,g_layout.quick[0].y+4);orcsdr::focus_nav::Rect focused{};assert(orcsdr::focus_nav::focused(&focused));
  const int update_clears=lgfx::screen_clears;
  assert(tap({g_layout.spectrum.x+g_layout.spectrum.w/2,g_layout.spectrum.y+36,1,1}).kind==ActionKind::tune_hz);
  assert(tap({g_layout.waterfall.x+g_layout.waterfall.w/2,g_layout.waterfall.y+8,1,1}).kind==ActionKind::tune_hz);
  // Value-only receiver updates repaint bounded widgets and keep the page clear count stable.
  lgfx::paints.clear();lgfx::fills.clear();
  const int api_clears=lgfx::screen_clears;
  assert(handle_control(Control::tune_frequency,7100000).kind==ActionKind::tune_hz);
  assert(handle_control(Control::tune_frequency,1).kind==ActionKind::none);
  s.audio_dsp_available=false;s.hunt_available=false;update(s);
  assert(handle_control(Control::audio_clean).kind==ActionKind::none);
  assert(handle_control(Control::hunt_start).kind==ActionKind::none);
  assert(lgfx::screen_clears==api_clears);
  s.audio_dsp_available=true;s.hunt_available=true;update(s);
  for(int i=0;i<4;++i){s.frequency_hz=7100000+uint32_t(i+1)*1000;s.relative_dbfs=-50.0f-float(i);update(s);}
  assert(lgfx::screen_clears==update_clears);
  bool latest=false;for(const auto& p:lgfx::paints)latest|=p.text.find("7.104.000 MHz")!=std::string::npos;
  assert(latest);
  orcsdr::focus_nav::Rect focused_after{};assert(orcsdr::focus_nav::focused(&focused_after));assert(focused_after.x==focused.x&&focused_after.y==focused.y&&focused_after.w==focused.w&&focused_after.h==focused.h);
  for(const auto& f:lgfx::fills)assert(!(f.x==g_layout.content.x&&f.y==g_layout.content.y&&f.w==g_layout.content.w&&f.h==g_layout.content.h));
  // Viewport changes are state preserving, including tab and keypad text.
  begin_frequency_entry();std::strcpy(g_entry,"7.104");
  const Tab tab=g_state.tab();const auto rotated=view.w<view.h?layout::Rect{view.x,view.y,view.h,view.w}:layout::Rect{view.x,view.y,view.h,view.w};
  assert(orcsdr::shortwave::set_viewport(rotated));assert(g_state.tab()==tab&&keypad_open()&&std::strcmp(g_entry,"7.104")==0);
  g_state.close_modal();draw();draw_spectrum(levels,0,160,-100.0f);assert(lgfx::live_sprite_bytes<=size_t(1230)*204*2);
  leave();
}
int main(){
  check_view({0,0,1280,720});check_view({0,0,800,480});
  check_view({72,72,424,1088});check_view({72,72,1088,424});
  puts("Shortwave production renderer/input recording checks PASS");
}
