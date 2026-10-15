// OrcSDR Home 720-square renderer. No USB Host or RF lifecycle ownership here.
#include "home_v3_ui.hpp"
#include "home_v3_drawer_layout.hpp"
#include "waterfall_style.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include <cstdio>
#include <cstring>
namespace orcsdr::waveshare4b::home_v3 {
namespace {
constexpr char TAG[]="ORCSDR_4B_HOME";
constexpr uint16_t CYAN=0x05ff,GREEN=0x6fe0,DIM=0x4228,PANEL=0x0021,GRID=0x2945;
constexpr uint32_t rgb(uint16_t c){
 return (uint32_t(c&0xf800)<<8)|(uint32_t(c&0x07e0)<<5)|(uint32_t(c&31)<<3);
}
extern const uint8_t badge_begin[] asm("_binary_orc_badge_104_rgb565_start");
extern const uint8_t badge_end[] asm("_binary_orc_badge_104_rgb565_end");
QueueHandle_t queue=nullptr;
Navigation nav{};
lv_obj_t *drawer=nullptr,*shade=nullptr,*scope=nullptr,*waterfall=nullptr;
lv_obj_t *freq=nullptr,*status=nullptr,*metrics=nullptr,*usb=nullptr,*uptime=nullptr,*step_text=nullptr,*palette_text=nullptr;
uint16_t *scope_data=nullptr,*waterfall_data=nullptr,*badge_data=nullptr;
bool had_rf=false;
lv_obj_t* frame(lv_obj_t* root,Rect r,uint16_t face=PANEL){
 auto* o=lv_obj_create(root);
 lv_obj_set_pos(o,r.x,r.y);lv_obj_set_size(o,r.w,r.h);
 lv_obj_set_style_bg_color(o,lv_color_hex(rgb(face)),0);
 lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_set_style_border_color(o,lv_color_hex(rgb(CYAN)),0);
 lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_radius(o,8,0);
 lv_obj_set_style_pad_all(o,0,0);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
 return o;
}
lv_obj_t* caption(lv_obj_t* root,Rect r,const char* value,uint16_t c,bool center=false){
 auto* o=lv_label_create(root);
 lv_obj_set_pos(o,r.x,r.y);lv_obj_set_size(o,r.w,r.h);
 lv_label_set_text(o,value);lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);
 lv_obj_set_style_text_color(o,lv_color_hex(rgb(c)),0);
 if(center)lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
 return o;
}
void sync_menu(){
 if(!drawer||!shade)return;
 if(nav.open){
  lv_obj_remove_flag(shade,LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(drawer,LV_OBJ_FLAG_HIDDEN);
 }else{
  lv_obj_add_flag(drawer,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(shade,LV_OBJ_FLAG_HIDDEN);
 }
 ESP_LOGI(TAG,"HOME_4B NAV %s",nav.open?"open":"closed");
}
void on_badge(lv_event_t*){nav.apply(NavAction::badge_tap);sync_menu();}
void on_shade(lv_event_t*){nav.apply(NavAction::outside_tap);sync_menu();}
void on_home(lv_event_t*){nav.apply(NavAction::item_selected);sync_menu();}
void on_command(lv_event_t* e){
 auto command=static_cast<Command>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
 if(queue&&(command==Command::Down||command==Command::Up||command==Command::Step))
   (void)xQueueSend(queue,&command,0);
}
void on_palette(lv_event_t*){
 waterfall_style::next_palette(waterfall_style::Screen::home);
 lv_label_set_text(palette_text,waterfall_style::palette_name(
  waterfall_style::palette(waterfall_style::Screen::home)));
}
lv_obj_t* button(lv_obj_t* root,Rect r,const char* name,Command cmd,bool enabled){
 auto* o=frame(root,r);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_set_style_border_color(o,lv_color_hex(rgb(enabled?CYAN:DIM)),0);
 caption(o,{2,(r.h-20)/2,r.w-4,20},name,enabled?GREEN:DIM,true);
 if(enabled)lv_obj_add_event_cb(o,on_command,LV_EVENT_CLICKED,
   reinterpret_cast<void*>(static_cast<intptr_t>(cmd)));
 else lv_obj_add_state(o,LV_STATE_DISABLED);
 return o;
}
bool make_canvas(lv_obj_t* root,Rect r,lv_obj_t** o,uint16_t** px){
 size_t size=size_t(r.w)*r.h*2;
 *px=static_cast<uint16_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!*px){ESP_LOGE(TAG,"PSRAM canvas allocation failed (%u)",unsigned(size));return false;}
 memset(*px,0,size);
 *o=lv_canvas_create(root);
 lv_canvas_set_buffer(*o,*px,r.w,r.h,LV_COLOR_FORMAT_RGB565);
 lv_obj_set_pos(*o,r.x,r.y);
 return true;
}
void make_badge(lv_obj_t* root){
 constexpr int n=62;
 if(badge_end-badge_begin==104*104*2)
  badge_data=static_cast<uint16_t*>(heap_caps_malloc(n*n*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(badge_data){
  // Original OrcSDR badge is packed big-endian RGB565; convert to LVGL native.
  for(int y=0;y<n;++y)for(int x=0;x<n;++x){
   int index=2*((y*104/n)*104+(x*104/n));
   badge_data[y*n+x]=static_cast<uint16_t>((uint16_t(badge_begin[index])<<8)|badge_begin[index+1]);
  }
  auto* image=lv_canvas_create(root);
  lv_canvas_set_buffer(image,badge_data,n,n,LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(image,kBadgeHit.x+1,kBadgeHit.y+2);
 }else{
  ESP_LOGE(TAG,"Orc badge unavailable");
  caption(root,kBadgeHit,"ORC",GREEN,true);
 }
 auto* hit=lv_obj_create(root);
 lv_obj_set_pos(hit,kBadgeHit.x,kBadgeHit.y);lv_obj_set_size(hit,kBadgeHit.w,kBadgeHit.h);
 lv_obj_set_style_bg_opa(hit,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(hit,0,0);lv_obj_set_style_pad_all(hit,0,0);
 lv_obj_add_flag(hit,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(hit,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_event_cb(hit,on_badge,LV_EVENT_CLICKED,nullptr);
}
void make_drawer(lv_obj_t* root){
 shade=lv_obj_create(root);
 lv_obj_set_pos(shade,kBackdrop.x,kBackdrop.y);lv_obj_set_size(shade,kBackdrop.w,kBackdrop.h);
 lv_obj_set_style_bg_color(shade,lv_color_hex(0),0);
 lv_obj_set_style_bg_opa(shade,LV_OPA_50,0);
 lv_obj_set_style_border_width(shade,0,0);lv_obj_set_style_pad_all(shade,0,0);
 lv_obj_add_flag(shade,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_remove_flag(shade,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_add_event_cb(shade,on_shade,LV_EVENT_CLICKED,nullptr);
 drawer=frame(root,kDrawer);
 caption(drawer,{12,10,260,26},"LAST USED",CYAN);
 const char* titles[]={"HOME","P25 RADIO","AM RADIO","SETTINGS","RF LAB","FM RADIO","ADS-B","ALL DASHBOARDS"};
 for(int i=0;i<8;++i){
  Rect local=kNavItems[i];local.x-=kDrawer.x;local.y-=kDrawer.y;
  const bool active=i==0;
  auto* row=frame(drawer,local);lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_color(row,lv_color_hex(rgb(active?GREEN:DIM)),0);
  char name[64];snprintf(name,sizeof(name),"%s%s",titles[i],active?"":" (soon)");
  caption(row,{9,14,local.w-18,20},name,active?GREEN:DIM);
  if(active)lv_obj_add_event_cb(row,on_home,LV_EVENT_CLICKED,nullptr);
  else lv_obj_add_state(row,LV_STATE_DISABLED);
 }
 lv_obj_add_flag(drawer,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(shade,LV_OBJ_FLAG_HIDDEN);
}
void zero_plots(){
 memset(scope_data,0,size_t(kSpectrumPlot.w)*kSpectrumPlot.h*2);
 memset(waterfall_data,0,size_t(kWaterfallPlot.w)*kWaterfallPlot.h*2);
 lv_obj_invalidate(scope);lv_obj_invalidate(waterfall);
}
void paint_iq(const uint8_t* bins){
 constexpr int w=kSpectrumPlot.w,h=kSpectrumPlot.h,fw=kWaterfallPlot.w,fh=kWaterfallPlot.h;
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  int line=h-1-int(bins[x*256/w])*(h-16)/255;
  scope_data[y*w+x]=x==w/2?DIM:(y==line||y==line+1)?GREEN:
    ((x%84==0||y%32==0)?GRID:0);
 }
 int rows=waterfall_style::rows_per_frame(waterfall_style::Screen::home);
 if(rows<1)rows=1;if(rows>fh)rows=fh;
 memmove(waterfall_data+rows*fw,waterfall_data,(fh-rows)*fw*sizeof(uint16_t));
 for(int y=0;y<rows;++y)for(int x=0;x<fw;++x)
  waterfall_data[y*fw+x]=waterfall_style::color565(waterfall_style::Screen::home,
       float(bins[x*256/fw])/255.f);
 lv_obj_invalidate(scope);lv_obj_invalidate(waterfall);
}
}
bool create(QueueHandle_t q){
 queue=q;nav={};had_rf=false;
 auto* screen=lv_screen_active();
 lv_obj_set_style_bg_color(screen,lv_color_hex(0),0);
 lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
 frame(screen,kSpectrumPanel);caption(screen,{26,96,590,23},"SPECTRUM / RELATIVE FFT",CYAN);
 frame(screen,kWaterfallPanel);caption(screen,{26,290,590,23},"WATERFALL / LIVE IQ",CYAN);
 frame(screen,kReadoutPanel);frame(screen,kControlsPanel);frame(screen,kFooter);
 if(!make_canvas(screen,kSpectrumPlot,&scope,&scope_data)||
    !make_canvas(screen,kWaterfallPlot,&waterfall,&waterfall_data))return false;
 freq=caption(screen,kFrequencyTouch,"96.100 MHz (TARGET)",GREEN);
 button(screen,kMode,"IQ ONLY",Command::FM,false);
 button(screen,kFilter,"FILTER --",Command::FM,false);
 button(screen,kSpan,"SPAN 960K",Command::FM,false);
 auto* step=button(screen,kStep,"STEP",Command::Step,true);
 step_text=caption(step,{2,25,kStep.w-4,17},"12500 Hz",CYAN,true);
 button(screen,kTuneDown,"TUNE -",Command::Down,true);
 button(screen,kTuneUp,"TUNE +",Command::Up,true);
 button(screen,kGain,"GAIN (soon)",Command::FM,false);
 button(screen,kBias,"BIAS (soon)",Command::FM,false);
 auto* p=frame(screen,kPalette);lv_obj_add_flag(p,LV_OBJ_FLAG_CLICKABLE);
 caption(p,{2,2,kPalette.w-4,18},"PALETTE",CYAN,true);
 palette_text=caption(p,{2,23,kPalette.w-4,19},"CLASSIC",GREEN,true);
 lv_obj_add_event_cb(p,on_palette,LV_EVENT_CLICKED,nullptr);
 button(screen,kMore,"MORE (soon)",Command::FM,false);
 status=caption(screen,{24,682,275,20},"NO SDR",CYAN);
 metrics=caption(screen,{307,682,380,20},"0 kS/s  drops 0  IQ ONLY",GREEN,true);
 caption(screen,kBrand,"OrcSDR\nHOME",0xffff);
 frame(screen,kHeaderStatus);
 caption(screen,{254,18,70,47},"Wi-Fi\nOFFLINE",DIM);
 usb=caption(screen,{329,18,90,47},"RTL-SDR\nNO SDR",DIM);
 uptime=caption(screen,{425,18,65,47},"UPTIME\n0m",CYAN);
 button(screen,kHeaderHome,"H",Command::FM,false);
 button(screen,kHeaderMute,"M",Command::FM,false);
 button(screen,kHeaderVisualizer,"VIS",Command::FM,false);
 button(screen,kHeaderSettings,"SET",Command::FM,false);
 make_drawer(screen);make_badge(screen);
 ESP_LOGI(TAG,"HOME_4B v3 original OrcSDR badge/palette, navigation, 668px IQ plot");
 return true;
}
void update(const Snapshot& s,const uint8_t* bins,bool fresh){
 char value[100];
 uint32_t hz=s.applied_hz?s.applied_hz:s.requested_hz;
 snprintf(value,sizeof(value),"%u.%03u MHz%s",unsigned(hz/1000000),
    unsigned((hz%1000000)/1000),s.applied_hz?"":" (TARGET)");
 lv_label_set_text(freq,value);
 snprintf(value,sizeof(value),"%u Hz",unsigned(s.step_hz));
 lv_label_set_text(step_text,value);
 const char* name=s.receiver_state==2?"RECEIVING IQ":s.receiver_state==1?"SEARCHING USB":
   s.receiver_state==3?"USB ERROR":"NO SDR";
 lv_label_set_text(status,name);
 lv_label_set_text(usb,s.receiver_state==2?"RTL-SDR\nIQ ACTIVE":"RTL-SDR\nNO SDR");
 snprintf(value,sizeof(value),"%u kS/s  drops %u  IQ ONLY",unsigned(s.effective_sps/1000),unsigned(s.drops));
 lv_label_set_text(metrics,value);
 snprintf(value,sizeof(value),"UPTIME\n%um",unsigned(s.uptime_sec/60));
 lv_label_set_text(uptime,value);
 if(s.receiver_state!=2){
  if(had_rf){zero_plots();had_rf=false;}
 }else if(fresh&&bins&&scope_data&&waterfall_data){
  had_rf=true;paint_iq(bins);
 }
}
}
