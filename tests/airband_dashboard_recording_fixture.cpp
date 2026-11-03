#define _CRT_SECURE_NO_WARNINGS
#include <esp_attr.h>
#define TFT_LIGHTGREY 0xc618
#include "../apps/orcsdr-tab5/ui/focus_nav.hpp"
#include <M5Unified.h>
#include "../apps/orcsdr-tab5/ui/airband_dashboard.cpp"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace orcsdr::audio_header {
const HeaderLayout& layout(){static HeaderLayout h=make_header_layout({0,0,1280,720});return h;}
bool set_viewport(HeaderRect){return true;}
void draw_brand(const char*){}
void draw_battery(int32_t){}
void draw_home_button(){}
void draw_mute_button(bool){}
void draw_visualizer_button(bool){}
void draw_settings_button(){}
bool home_hit(int32_t,int32_t){return false;}
bool settings_hit(int32_t,int32_t){return false;}
}
namespace orcsdr::airband {
const char* spacing_name(Spacing){return "25 kHz";}const char* source_name(ScanSource){return "FULL BAND";}const char* state_name(ScanState){return "SCANNING";}
const char* service_name(Service){return "TOWER";}const char* source_class_name(SourceClass){return "OFFICIAL";}
}
namespace orcsdr::receiver_controls {
Item item(Control c,const State&s){
  if(c==Control::rf_gain)return {"GAIN","",s.capabilities.rf_gain?Availability::enabled:Availability::unavailable,false,s.gain_tenth_db};
  if(c==Control::tuner_agc)return {"AGC","",s.capabilities.tuner_agc?Availability::enabled:Availability::unavailable,s.tuner_agc,0};
  if(c==Control::rtl_agc)return {"RTL AGC","",s.capabilities.rtl_agc?Availability::enabled:Availability::unavailable,s.rtl_agc,0};
  return {};
}
}
namespace orcsdr::waterfall_style {
const char* palette_name(uint8_t){return "BLUE";} const char* speed_name(uint8_t){return "NORMAL";}
uint8_t palette(Screen){return 0;}uint8_t speed(Screen){return 0;}void next_palette(Screen){}void next_speed(Screen){}
int rows_due(Screen,uint32_t,uint32_t){return 1;}uint16_t color565(Screen,float){return 0xffff;}
}

int main(){
  using namespace orcsdr::airband;
  const layout::Rect viewports[]={{0,0,800,480},{72,72,424,1088},{72,72,1088,424},{0,0,1280,720}};
  for(const auto viewport:viewports){
    Snapshot s{};s.frequency_hz=124900000;s.running=true;s.catalog_loaded=true;s.location_configured=true;
    s.catalog_count=32;s.bank_count=32;s.activity_count=32;s.stops=UINT32_MAX;s.channels_checked=UINT32_MAX;
    s.controls.capabilities.rf_gain=true;s.controls.capabilities.tuner_agc=true;s.controls.capabilities.rtl_agc=true;
    s.scan.spacing=Spacing::khz833;
    for(int i=0;i<32;++i){std::snprintf(s.catalog[i].label,sizeof(s.catalog[i].label),"Airport %02d with long station context",i);s.catalog[i].frequency_hz=124000000+i*25000;
      std::snprintf(s.bank[i].label,sizeof(s.bank[i].label),"Bank %02d",i);s.bank[i].frequency_hz=125000000+i*25000;
      std::snprintf(s.activity[i].label,sizeof(s.activity[i].label),"Activity %02d with a long retained description",i);s.activity[i].frequency_hz=126000000+i*25000;s.activity[i].duration_ms=UINT32_MAX;s.activity[i].peak_snr_db=127.0f;}
    assert(dashboard_set_viewport(viewport));dashboard_enter(s);
    for(int tab=0;tab<6;++tab){dashboard_handle_touch(cx(g_layout.tab[tab]),cy(g_layout.tab[tab]));assert(dashboard_tab()==static_cast<Tab>(tab));}
    dashboard_select_tab(Tab::airports);
    const size_t page_size=static_cast<size_t>(g_layout.airport_row_count);
    const size_t airport_pages=(32+page_size-1)/page_size;
    for(size_t p=1;p<airport_pages;++p)dashboard_handle_touch(cx(g_layout.airport_next),cy(g_layout.airport_next));
    const size_t final_airport_row=31-g_airport_page*page_size;
    lgfx::paints.clear();
    const Action tune=dashboard_handle_touch(cx(g_layout.airport_rows[final_airport_row]),cy(g_layout.airport_rows[final_airport_row]));
    assert(tune.kind==ActionKind::tune_catalog&&tune.value==31);
    std::string airport_detail;for(const auto&p:lgfx::paints)airport_detail+=p.text;assert(airport_detail.find("Airport 31 with long station context")!=std::string::npos);
    dashboard_handle_touch(g_layout.content.x+g_layout.content.w-40,g_layout.content.y+40);
    dashboard_select_tab(Tab::activity);
    const size_t activity_pages=(32+static_cast<size_t>(g_layout.activity_row_count)-1)/static_cast<size_t>(g_layout.activity_row_count);
    for(size_t p=1;p<activity_pages;++p)dashboard_handle_touch(cx(g_layout.activity_next),cy(g_layout.activity_next));
    const size_t final_activity_row=31-g_activity_page*static_cast<size_t>(g_layout.activity_row_count);
    const Action activity=dashboard_handle_touch(cx(g_layout.activity_rows[final_activity_row]),cy(g_layout.activity_rows[final_activity_row]));
    assert(activity.kind==ActionKind::tune_catalog&&activity.value==-32);
    dashboard_handle_touch(g_layout.content.x+g_layout.content.w-40,g_layout.content.y+40);
    dashboard_select_tab(Tab::scan);
    const size_t bank_pages=(32+static_cast<size_t>(g_layout.scan_row_count)-1)/static_cast<size_t>(g_layout.scan_row_count);
    for(size_t p=1;p<bank_pages;++p)dashboard_handle_touch(cx(g_layout.scan_next),cy(g_layout.scan_next));
    const size_t final_bank_row=31-g_bank_page*static_cast<size_t>(g_layout.scan_row_count);
    const Action bank=dashboard_handle_touch(cx(g_layout.scan_rows[final_bank_row]),cy(g_layout.scan_rows[final_bank_row]));
    assert(bank.kind==ActionKind::tune_catalog&&bank.value==31);
    dashboard_handle_touch(g_layout.content.x+g_layout.content.w-40,g_layout.content.y+40);
    dashboard_select_tab(Tab::setup);
    if(g_layout.setup_page_count>1){dashboard_handle_touch(cx(g_layout.setup_next),cy(g_layout.setup_next));assert(g_setup_page==1);dashboard_handle_touch(cx(g_layout.setup_previous),cy(g_layout.setup_previous));assert(g_setup_page==0);}
    const auto setup_action=dashboard_handle_touch(cx(g_layout.setup_value[0]),cy(g_layout.setup_value[0]));assert(setup_action.kind==ActionKind::spacing_cycle);
    const Rect focus=g_layout.setup_value[0];orcsdr::focus_nav::focus_at(cx(focus),cy(focus));
    dashboard_select_tab(Tab::listen);
    const Action listen_scan=dashboard_handle_touch(cx(g_layout.control[0]),cy(g_layout.control[0]));assert(listen_scan.kind==ActionKind::scan_toggle);
    bool applied=false;const Action named=dashboard_control(Control::tune_up,0,&applied);assert(applied&&named.kind==ActionKind::tune_up);
    assert(dashboard_control(Control::tune_to,100,&applied).kind==ActionKind::none&&!applied);
    assert(dashboard_control(Control::filter_set,4500,&applied).kind==ActionKind::none&&!applied);
    dashboard_control(Control::select_tab,static_cast<int32_t>(Tab::setup),&applied);assert(applied&&dashboard_tab()==Tab::setup);
    dashboard_control(Control::select_tab,static_cast<int32_t>(Tab::listen),&applied);assert(applied&&dashboard_tab()==Tab::listen);
    orcsdr::focus_nav::focus_at(cx(g_layout.control[4]),cy(g_layout.control[4]));
    orcsdr::focus_nav::Rect focused_before{};assert(orcsdr::focus_nav::focused(&focused_before));
    const int clears=lgfx::screen_clears;lgfx::paints.clear();lgfx::fills.clear();
    s.stops=UINT32_MAX-1;s.now_ms=900;dashboard_update(s);
    s.stops=UINT32_MAX;s.now_ms=1000;
    s.frequency_hz+=25000;s.now_ms=1000;dashboard_update(s);
    assert(lgfx::screen_clears==clears);
    bool frequency=false;for(const auto&p:lgfx::paints)frequency|=p.text.find("124.925")!=std::string::npos;
    assert(frequency);
    bool max_counter=false;for(const auto&p:lgfx::paints)max_counter=max_counter||p.text.find("4294967295")!=std::string::npos;assert(max_counter);
    assert(std::none_of(lgfx::fills.begin(),lgfx::fills.end(),[](const auto&r){return r.x==g_layout.content.x&&r.y==g_layout.content.y&&r.w==g_layout.content.w&&r.h==g_layout.content.h;}));
    orcsdr::focus_nav::Rect focused{};const bool had_focus=orcsdr::focus_nav::focused(&focused);
    for(int i=0;i<4;++i){s.now_ms+=250;s.snr_db=float(i*3);dashboard_update(s);}
    assert(lgfx::screen_clears==clears);
    if(had_focus){orcsdr::focus_nav::Rect after{};assert(orcsdr::focus_nav::focused(&after));assert(after.x==focused_before.x&&after.y==focused_before.y);}
    dashboard_select_tab(Tab::activity);s.now_ms=6000;lgfx::fills.clear();dashboard_update(s);
    assert(lgfx::screen_clears==clears);
    assert(std::none_of(lgfx::fills.begin(),lgfx::fills.end(),[](const auto&r){return r.x==g_layout.content.x&&r.y==g_layout.content.y&&r.w==g_layout.content.w&&r.h==g_layout.content.h;}));
    dashboard_select_tab(Tab::scope);s.frequency_hz=124900000;dashboard_update(s);
    static constexpr ActionKind scope_actions[13]={ActionKind::tune_down,ActionKind::tune_up,ActionKind::span_down,ActionKind::span_up,ActionKind::gain_down,ActionKind::gain_up,ActionKind::squelch_down,ActionKind::squelch_up,ActionKind::filter_down,ActionKind::filter_up,ActionKind::tuner_agc_toggle,ActionKind::scan_toggle,ActionKind::tune_guard};
    for(int i=0;i<13;++i){const Action got=dashboard_handle_touch(cx(g_layout.scope_control[i]),cy(g_layout.scope_control[i]));assert(got.kind==scope_actions[i]);}
    float levels[256]{};for(int i=0;i<256;++i)levels[i]=-80.0f+float(i%31);
    dashboard_draw_spectrum(levels,0,256,480000);
    const auto target=viewport.w==424?layout::Rect{72,72,1088,424}:viewport.w==1088?layout::Rect{72,72,424,1088}:viewport;
    assert(dashboard_set_viewport(target));assert(dashboard_tab()==Tab::scope);assert(g_snapshot.scan.spacing==Spacing::khz833);
    dashboard_leave();
  }
  std::puts("Airband production renderer/input recording tests PASS");
}
