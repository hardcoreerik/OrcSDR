// Waveshare 4B OrcSDR FM five-tab LVGL receiver shell.
// Refer to Tab5 fm_dashboard.cpp; display-only, truthful IQ-only until DSP port.
#include "fm_4b_ui.hpp"
#include "fm_4b_layout.hpp"
#include "waterfall_style.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace orcsdr::waveshare4b::fm {
namespace {
constexpr char TAG[]="ORCSDR_4B_FM";
constexpr uint16_t BG=0x0000,PANEL=0x0841,CYAN=0x2e7f,GREEN=0x6fe8,MUTED=0x8c71,GRID=0x2945;
constexpr uint32_t to_rgb(uint16_t c) {
 return (uint32_t(c&0xf800)<<8)|(uint32_t(c&0x07e0)<<5)|(uint32_t(c&31)<<3);
}
extern const uint8_t badge_start[] asm("_binary_orc_badge_104_rgb565_start");
extern const uint8_t badge_end[] asm("_binary_orc_badge_104_rgb565_end");
QueueHandle_t commands=nullptr;
void (*go_home)()=nullptr;
lv_obj_t *root=nullptr,*page=nullptr,*shade=nullptr,*menu=nullptr;
lv_obj_t *tab_buttons[kTabs]{};
lv_obj_t *head_status=nullptr,*freq_label=nullptr,*aux_label=nullptr;
lv_obj_t *health_values[8]{};
lv_obj_t *scope=nullptr,*fall=nullptr;
uint16_t *scope_data=nullptr,*fall_data=nullptr,*badge_data=nullptr;
Model model{};
bool is_open=false,drawer_open=false,live_plot=false;
uint32_t last_hz=96100000;
constexpr int kBadgeWidth=62;
lv_obj_t* box(lv_obj_t* parent,Rect r,uint16_t bg=PANEL,uint16_t border=CYAN) {
 auto* o=lv_obj_create(parent);
 lv_obj_set_pos(o,r.x,r.y);
 lv_obj_set_size(o,r.w,r.h);
 lv_obj_set_style_bg_color(o,lv_color_hex(to_rgb(bg)),0);
 lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_set_style_border_color(o,lv_color_hex(to_rgb(border)),0);
 lv_obj_set_style_border_width(o,1,0);
 lv_obj_set_style_radius(o,7,0);
 lv_obj_set_style_pad_all(o,0,0);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
 return o;
}
lv_obj_t* txt(lv_obj_t* parent,Rect r,const char* value,uint16_t color=GREEN,bool center=false) {
 auto* o=lv_label_create(parent);
 lv_obj_set_pos(o,r.x,r.y);
 lv_obj_set_size(o,r.w,r.h);
 lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);
 lv_obj_set_style_text_color(o,lv_color_hex(to_rgb(color)),0);
 if(center)lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
 lv_label_set_text(o,value);
 return o;
}
// Screen locations are absolute in fm_4b_layout; page children are page-local.
constexpr Rect local(Rect r){return {r.x-kBody.x,r.y-kBody.y,r.w,r.h};}
void navigate_home(lv_event_t*) {
 drawer_open=false;
 if(menu)lv_obj_add_flag(menu,LV_OBJ_FLAG_HIDDEN);
 if(shade)lv_obj_add_flag(shade,LV_OBJ_FLAG_HIDDEN);
 is_open=false;
 if(root)lv_obj_add_flag(root,LV_OBJ_FLAG_HIDDEN);
 if(go_home)go_home();
}
void toggle_drawer(lv_event_t*) {
 drawer_open=!drawer_open;
 if(drawer_open) {
  lv_obj_remove_flag(shade,LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(menu,LV_OBJ_FLAG_HIDDEN);
 }else {
  lv_obj_add_flag(menu,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(shade,LV_OBJ_FLAG_HIDDEN);
 }
 ESP_LOGI(TAG,"FM NAV %s",drawer_open?"open":"closed");
}
void dismiss_drawer(lv_event_t*) {
 drawer_open=false;
 lv_obj_add_flag(menu,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_flag(shade,LV_OBJ_FLAG_HIDDEN);
}
void tune(lv_event_t* e) {
 if(!commands)return;
 const intptr_t d=reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
 const auto command=static_cast<home_v3::Command>(d);
 // UI guard. Generic Home radio worker still owns retune and USB lifecycle.
 if(command==home_v3::Command::Down && last_hz<=kMinHz)return;
 if(command==home_v3::Command::Up && last_hz>=kMaxHz)return;
 if(command==home_v3::Command::Down||command==home_v3::Command::Up||command==home_v3::Command::Step)
  (void)xQueueSend(commands,&command,0);
}
void palette_tap(lv_event_t*) {
 waterfall_style::next_palette(waterfall_style::Screen::fm);
 ESP_LOGI(TAG,"FM waterfall palette %s",
   waterfall_style::palette_name(waterfall_style::palette(waterfall_style::Screen::fm)));
}
lv_obj_t* btn(lv_obj_t* parent,Rect r,const char* title,bool enabled=false,
               home_v3::Command command=home_v3::Command::FM){
 auto* o=box(parent,r,PANEL,enabled?CYAN:MUTED);
 lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
 txt(o,{2,(r.h-22)/2,r.w-4,22},title,enabled?GREEN:MUTED,true);
 if(enabled)
  lv_obj_add_event_cb(o,tune,LV_EVENT_CLICKED,
                       reinterpret_cast<void*>(static_cast<intptr_t>(command)));
 else lv_obj_add_state(o,LV_STATE_DISABLED);
 return o;
}
void tab_tap(lv_event_t* e);
void build_page();
void set_tab(Tab next){
 if(model.selected==next)return;
 model.selected=next;
 for(unsigned i=0;i<kTabs;++i) {
  lv_obj_set_style_border_color(tab_buttons[i],
    lv_color_hex(to_rgb(i==index(next)?GREEN:GRID)),0);
 }
 build_page();
 ESP_LOGI(TAG,"FM TAB %s",kTabNames[index(next)]);
}
void tab_tap(lv_event_t* e){
 unsigned i=static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
 if(i<kTabs)set_tab(from_index(i));
}
void badge(lv_obj_t* parent) {
 if(badge_end-badge_start==104*104*2)
  badge_data=static_cast<uint16_t*>(heap_caps_malloc(kBadgeWidth*kBadgeWidth*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(badge_data){
  for(int y=0;y<kBadgeWidth;++y)for(int x=0;x<kBadgeWidth;++x){
   const int i=((y*104/kBadgeWidth)*104+x*104/kBadgeWidth)*2;
   badge_data[y*kBadgeWidth+x]=static_cast<uint16_t>((uint16_t(badge_start[i])<<8)|badge_start[i+1]);
  }
  auto* image=lv_canvas_create(parent);
  lv_canvas_set_buffer(image,badge_data,kBadgeWidth,kBadgeWidth,LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(image,kBadge.x+1,kBadge.y+2);
 }else{
  ESP_LOGE(TAG,"OrcSDR badge image unavailable");
  txt(parent,kBadge,"ORC",GREEN,true);
 }
 auto* b=lv_obj_create(parent);
 lv_obj_set_pos(b,kBadge.x,kBadge.y);lv_obj_set_size(b,kBadge.w,kBadge.h);
 lv_obj_set_style_bg_opa(b,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(b,0,0);
 lv_obj_set_style_pad_all(b,0,0);
 lv_obj_remove_flag(b,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(b,toggle_drawer,LV_EVENT_CLICKED,nullptr);
}
void make_menu() {
 shade=lv_obj_create(root);
 lv_obj_set_pos(shade,300,86);
 lv_obj_set_size(shade,406,540);
 lv_obj_set_style_bg_color(shade,lv_color_hex(0),0);
 lv_obj_set_style_bg_opa(shade,LV_OPA_50,0);
 lv_obj_set_style_border_width(shade,0,0);
 lv_obj_set_style_pad_all(shade,0,0);
 lv_obj_add_flag(shade,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_remove_flag(shade,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_event_cb(shade,dismiss_drawer,LV_EVENT_CLICKED,nullptr);
 menu=box(root,{14,86,286,540});
 txt(menu,{10,13,260,24},"DASHBOARDS",CYAN);
 auto* home=box(menu,{12,67,260,57},PANEL,GREEN);
 txt(home,{12,18,232,22},"HOME",GREEN);
 lv_obj_add_flag(home,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(home,navigate_home,LV_EVENT_CLICKED,nullptr);
 auto* fm=box(menu,{12,133,260,57},PANEL,GREEN);
 txt(fm,{12,18,232,22},"FM BROADCAST  /  IQ",GREEN);
 lv_obj_add_flag(fm,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(fm,dismiss_drawer,LV_EVENT_CLICKED,nullptr);
 txt(menu,{20,216,250,86},"Other dashboards are\nnot ported on 4B.",MUTED);
 lv_obj_add_flag(shade,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_flag(menu,LV_OBJ_FLAG_HIDDEN);
}
void card(Rect r,const char* title,const char* value,uint16_t value_color=GREEN) {
 auto* parent=box(page,local(r));
 txt(parent,{12,10,r.w-24,22},title,CYAN);
 txt(parent,{12,40,r.w-24,r.h-47},value,value_color);
}
void page_listen(){
 card(kListenFrequency,"FM FREQUENCY","WAITING FOR SDR");
 card(kListenSignal,"RELATIVE RF SIGNAL","IQ ONLY / NO AUDIO",MUTED);
 card(kListenPreset,"PRESET MEMORY","NOT PORTED",MUTED);
 card(kListenAudio,"STEREO VU / STATION","AUDIO & RDS NOT AVAILABLE",MUTED);
 auto* p=box(page,local(kListenActions));
 txt(p,{12,6,650,21},"FM CONTROLS  /  SEEK & KEYPAD PENDING",CYAN);
 btn(p,{10,35,120,45},"SEEK -",false);
 btn(p,{145,35,120,45},"TUNE -",true,home_v3::Command::Down);
 btn(p,{280,35,120,45},"STEP",true,home_v3::Command::Step);
 btn(p,{415,35,120,45},"TUNE +",true,home_v3::Command::Up);
 btn(p,{550,35,120,45},"SEEK +",false);
}
void page_spectrum(){
 card(kSpectrumSummary,"FM RF SPECTRUM / IQ-ONLY","960 kS/s RAW CU8; NO DEMOD FILTER");
 auto* s=box(page,{7,98,666,145});
 (void)s;
 auto* w=box(page,{7,248,666,146});
 (void)w;
 if(scope_data){
  scope=lv_canvas_create(page);
  lv_canvas_set_buffer(scope,scope_data,kSpectrumPlot.w,kSpectrumPlot.h,LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(scope,kSpectrumPlot.x-kBody.x,kSpectrumPlot.y-kBody.y);
 }
 if(fall_data){
  fall=lv_canvas_create(page);
  lv_canvas_set_buffer(fall,fall_data,kWaterfallPlot.w,kWaterfallPlot.h,LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(fall,kWaterfallPlot.x-kBody.x,kWaterfallPlot.y-kBody.y);
 }
 auto* actions=box(page,local(kSpectrumActions));
 btn(actions,{7,1,139,41},"SPAN N/A",false);
 btn(actions,{152,1,142,41},"TUNE -",true,home_v3::Command::Down);
 btn(actions,{300,1,142,41},"TUNE +",true,home_v3::Command::Up);
 auto* pal=btn(actions,{448,1,224,41},"FM PALETTE",false);
 lv_obj_remove_state(pal,LV_STATE_DISABLED);
 lv_obj_set_style_border_color(pal,lv_color_hex(to_rgb(GREEN)),0);
 lv_obj_add_event_cb(pal,palette_tap,LV_EVENT_CLICKED,nullptr);
}
void page_station(){
 card(kStationSummary,"NOW PLAYING","NO RDS DECODER ON 4B",MUTED);
 card(kStationPS,"PS / PROGRAM SERVICE","UNAVAILABLE",MUTED);
 card(kStationPI,"PI / PROGRAM IDENTIFIER","UNAVAILABLE",MUTED);
 card(kStationPTY,"PTY / PROGRAM TYPE","UNAVAILABLE",MUTED);
 card(kStationStereo,"STEREO / PILOT LOCK","NOT MEASURED",MUTED);
 card(kStationRT,"RADIO TEXT","Receiver IQ is live when connected.\nRDS decoding is not yet wired in.\nNo station data will be fabricated.",MUTED);
}
void page_health(){
 static const char* names[]={"RECEIVER","EFFECTIVE SPS","TARGET SPS","IQ DROPS (TOTAL)",
  "USB OVERRUNS","DSP CPU","AUDIO UNDERRUNS","DRIVER"};
 for(unsigned i=0;i<8;++i){
  auto* c=box(page,local(kHealthCards[i]));
  txt(c,{12,9,kHealthCards[i].w-24,22},names[i],CYAN);
  health_values[i]=txt(c,{12,41,kHealthCards[i].w-24,28},"N/A",i<4?GREEN:MUTED);
 }
 auto* footer=box(page,local(kHealthFooter));
 txt(footer,{7,4,660,20},"Unmeasured counters show N/A (never zero).",MUTED);
}
void page_settings(){
 auto* radio=box(page,local(kSettingsRadio));
 txt(radio,{12,13,305,25},"FM TUNING / SPECTRUM",CYAN);
 btn(radio,{12,70,305,51},"STEP SIZE",true,home_v3::Command::Step);
 auto* pal=btn(radio,{12,134,305,51},"CYCLE FM PALETTE",false);
 lv_obj_remove_state(pal,LV_STATE_DISABLED);
 lv_obj_set_style_border_color(pal,lv_color_hex(to_rgb(GREEN)),0);
 lv_obj_add_event_cb(pal,palette_tap,LV_EVENT_CLICKED,nullptr);
 btn(radio,{12,199,305,51},"FILTER BW: NOT PORTED",false);
 btn(radio,{12,264,305,51},"RF GAIN: NOT PORTED",false);
 btn(radio,{12,329,305,51},"PRESETS: NOT PORTED",false);
 auto* audio=box(page,local(kSettingsAudio));
 txt(audio,{12,13,308,25},"FM AUDIO / OPERATIONS",CYAN);
 btn(audio,{12,70,313,51},"SOUND: NOT PORTED",false);
 btn(audio,{12,134,313,51},"VOLUME: NOT PORTED",false);
 btn(audio,{12,199,313,51},"RECORDING: NOT PORTED",false);
 btn(audio,{12,264,313,51},"SCAN: NOT PORTED",false);
 btn(audio,{12,329,313,51},"DEVICE SETTINGS: N/A",false);
}
void build_page(){
 scope=nullptr;fall=nullptr;freq_label=nullptr;aux_label=nullptr;
 for(auto& p:health_values)p=nullptr;
 lv_obj_clean(page);
 switch(model.selected){
  case Tab::listen:page_listen();break;
  case Tab::spectrum:page_spectrum();break;
  case Tab::station_rds:page_station();break;
  case Tab::rf_health:page_health();break;
  case Tab::settings:page_settings();break;
  default:break;
 }
}
bool allocate_buffers(){
 const size_t ss=size_t(kSpectrumPlot.w)*kSpectrumPlot.h*2;
 const size_t ws=size_t(kWaterfallPlot.w)*kWaterfallPlot.h*2;
 scope_data=static_cast<uint16_t*>(heap_caps_malloc(ss,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 fall_data=static_cast<uint16_t*>(heap_caps_malloc(ws,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!scope_data||!fall_data){
  ESP_LOGE(TAG,"FM PSRAM canvas allocation failed");
  return false;
 }
 memset(scope_data,0,ss);
 memset(fall_data,0,ws);
 return true;
}
void plot(const uint8_t* data){
 const int w=kSpectrumPlot.w,h=kSpectrumPlot.h,fw=kWaterfallPlot.w,fh=kWaterfallPlot.h;
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  int line=h-1-int(data[x*256/w])*(h-16)/255;
  scope_data[y*w+x]=x==w/2?GRID:(y==line||y==line+1)?GREEN:
      ((x%84==0||y%30==0)?GRID:BG);
 }
 int rows=waterfall_style::rows_per_frame(waterfall_style::Screen::fm);
 if(rows<1)rows=1;
 if(rows>fh)rows=fh;
 memmove(fall_data+rows*fw,fall_data,(fh-rows)*fw*sizeof(uint16_t));
 for(int y=0;y<rows;++y)for(int x=0;x<fw;++x)
  fall_data[y*fw+x]=waterfall_style::color565(waterfall_style::Screen::fm,
     float(data[x*256/fw])/255.f);
 if(scope)lv_obj_invalidate(scope);
 if(fall)lv_obj_invalidate(fall);
}
void clear_plot(){
 if(scope_data)memset(scope_data,0,size_t(kSpectrumPlot.w)*kSpectrumPlot.h*2);
 if(fall_data)memset(fall_data,0,size_t(kWaterfallPlot.w)*kWaterfallPlot.h*2);
 if(scope)lv_obj_invalidate(scope);
 if(fall)lv_obj_invalidate(fall);
}
}
bool create(QueueHandle_t q,void(*home_cb)()){
 commands=q;go_home=home_cb;
 if(!allocate_buffers())return false;
 root=lv_obj_create(lv_screen_active());
 lv_obj_set_pos(root,0,0);lv_obj_set_size(root,720,720);
 lv_obj_set_style_bg_color(root,lv_color_hex(to_rgb(BG)),0);
 lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
 lv_obj_set_style_border_width(root,0,0);
 lv_obj_set_style_pad_all(root,0,0);
 lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
 txt(root,kBrand,"OrcSDR\nFM BROADCAST",0xffff);
 box(root,kStatus);
 txt(root,{253,20,80,45},"Wi-Fi\nOFFLINE",MUTED);
 head_status=txt(root,{336,20,155,45},"RTL-SDR\nNO SDR",MUTED);
 auto* home=btn(root,kHome,"H",false);
 lv_obj_remove_state(home,LV_STATE_DISABLED);
 lv_obj_set_style_border_color(home,lv_color_hex(to_rgb(GREEN)),0);
 lv_obj_add_event_cb(home,navigate_home,LV_EVENT_CLICKED,nullptr);
 btn(root,kMute,"M",false);
 btn(root,kVisual,"VIS",false);
 btn(root,kGear,"SET",false);
 box(root,kPageTitle);
 txt(root,{25,101,490,22},"FM BROADCAST",0xffff);
 txt(root,{25,131,668,22},"IQ PREVIEW - AUDIO & RDS NOT YET PORTED",MUTED);
 page=box(root,kBody,BG,BG);
 for(unsigned i=0;i<kTabs;++i){
  auto b=box(root,kTabsRect[i],PANEL,i==0?GREEN:GRID);
  tab_buttons[i]=b;
  txt(b,{5,23,kTabsRect[i].w-10,24},kTabNames[i],i==0?GREEN:CYAN,true);
  lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b,tab_tap,LV_EVENT_CLICKED,reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
 }
 model={};
 build_page();
 make_menu();
 badge(root);
 lv_obj_add_flag(root,LV_OBJ_FLAG_HIDDEN);
 ESP_LOGI(TAG,"FM 4B created with five original tabs; IQ-only, no WFM audio or RDS");
 return true;
}
void open(){
 if(!root)return;
 model.selected=Tab::listen;
 for(unsigned i=0;i<kTabs;++i)
  lv_obj_set_style_border_color(tab_buttons[i],lv_color_hex(to_rgb(i==0?GREEN:GRID)),0);
 build_page();
 drawer_open=false;
 lv_obj_add_flag(shade,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_flag(menu,LV_OBJ_FLAG_HIDDEN);
 is_open=true;
 lv_obj_remove_flag(root,LV_OBJ_FLAG_HIDDEN);
 lv_obj_move_foreground(root);
 ESP_LOGI(TAG,"FM 4B IQ preview opened");
}
void close(){
 if(root)lv_obj_add_flag(root,LV_OBJ_FLAG_HIDDEN);
 drawer_open=false;is_open=false;
 ESP_LOGI(TAG,"FM 4B closed");
}
bool active(){return is_open;}
void update(const home_v3::Snapshot& s,const uint8_t* iq,bool fresh){
 if(!is_open||!root)return;
 const uint32_t hz=s.applied_hz?s.applied_hz:s.requested_hz;
 last_hz=hz;
 char buf[100];
 const char* condition=s.receiver_state==2?"IQ ACTIVE":s.receiver_state==1?"USB SEARCH":
    s.receiver_state==3?"USB ERROR":"NO SDR";
 snprintf(buf,sizeof(buf),"RTL-SDR\n%s",condition);
 lv_label_set_text(head_status,buf);
 if(model.selected==Tab::listen){
  // Directly update the view's card value, never synthetic station metadata.
  auto* frequency_card=lv_obj_get_child(page,0);
  if(frequency_card){
   auto* value=lv_obj_get_child(frequency_card,1);
   if(value){
    snprintf(buf,sizeof(buf),"%u.%03u MHz%s",unsigned(hz/1000000),
      unsigned((hz%1000000)/1000),s.applied_hz?"":" (TARGET)");
    lv_label_set_text(value,buf);
   }
  }
  auto* signal_card=lv_obj_get_child(page,1);
  if(signal_card){
   auto* value=lv_obj_get_child(signal_card,1);
   if(value)lv_label_set_text(value,s.receiver_state==2?"RECEIVING RAW IQ":"NO IQ STREAM");
  }
 }else if(model.selected==Tab::rf_health){
  const char* values[8]={"","","","","N/A","N/A","N/A","v0.9.3 PIN"};
  values[0]=condition;
  snprintf(buf,sizeof(buf),"%u",unsigned(s.effective_sps));values[1]=buf;
  char target[32],dropped[32];
  snprintf(target,sizeof(target),"%u",960000u);values[2]=target;
  snprintf(dropped,sizeof(dropped),"%u",unsigned(s.drops));values[3]=dropped;
  for(unsigned i=0;i<8;++i)if(health_values[i])lv_label_set_text(health_values[i],values[i]);
 }
 if(s.receiver_state!=2){
  if(live_plot){clear_plot();live_plot=false;}
 }else if(fresh&&iq&&scope_data&&fall_data&&model.selected==Tab::spectrum){
  live_plot=true;plot(iq);
 }
}
}
