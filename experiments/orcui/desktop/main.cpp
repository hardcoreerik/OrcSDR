// OrcUI native desktop simulator: real LVGL 9 + SDL2, shared Home and RF rasters.
// RF source is SYNTHETIC. No SDR driver is loaded.
#include "orcui/home_lvgl.hpp"
#include <lvgl.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <thread>
namespace {
using namespace orcsdr::orcui;
struct Preset { const char* key; const char* title; int w,h; Shape shape; float zoom; };
constexpr Preset presets[]={
 {"waveshare","Waveshare 720x720",720,720,Shape::rectangle,1.f},
 {"tab5","Tab5 1280x720",1280,720,Shape::rectangle,.85f},
 {"round480","Round 480",480,480,Shape::circle,1.f},
 {"round360","Round 360",360,360,Shape::circle,1.f},
 {"ultrawide","Ultrawide 2560x720",2560,720,Shape::rectangle,.5f},
 {"portrait","Portrait 480x800",480,800,Shape::rectangle,1.f},
 {"compact","Compact 800x480",800,480,Shape::rectangle,1.f},
};
HomeLvgl home;
lv_display_t* display=nullptr;
lv_obj_t* toolbar=nullptr;
lv_obj_t* picker=nullptr;
lv_obj_t* pause_button=nullptr;
bool paused=false;
int selected=0, pending=-1;
uint32_t touches=0,span=960000;
HomeData data{};
std::array<float,512> fft{};
void action(HomeCommand cmd,void*){
 ++touches;
 switch(cmd){
  case HomeCommand::span_up: span=std::min(2400000u,span*2);break;
  case HomeCommand::span_down:span=std::max(120000u,span/2);break;
  case HomeCommand::step_down:data.frequency_hz-=12500;break;
  case HomeCommand::step_up:data.frequency_hz+=12500;break;
  case HomeCommand::mode:data.mode="MODE SIM";break;
  case HomeCommand::filter:data.mode="FILTER SIM";break;
  case HomeCommand::gain:data.mode="GAIN SIM";break;
  default:break;
 }
 std::printf("orcui_action=%d touches=%u span_hz=%u\n",static_cast<int>(cmd),touches,span);
 std::fflush(stdout);
}
void picker_changed(lv_event_t* e){
 if(lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED)
   pending=lv_dropdown_get_selected(picker);
}
void paused_clicked(lv_event_t* e){
 if(lv_event_get_code(e)!=LV_EVENT_CLICKED)return;
 paused=!paused;
 lv_label_set_text(lv_obj_get_child(pause_button,0),paused?"RESUME":"PAUSE");
}
void create_toolbar(){
 toolbar=lv_obj_create(lv_screen_active());
 lv_obj_set_size(toolbar,330,52);
 lv_obj_align(toolbar,LV_ALIGN_TOP_RIGHT,-4,4);
 lv_obj_set_style_bg_color(toolbar,lv_color_hex(0x00131a),0);
 lv_obj_set_style_bg_opa(toolbar,LV_OPA_COVER,0);
 lv_obj_set_style_border_color(toolbar,lv_color_hex(0x00cfff),0);
 lv_obj_set_style_border_width(toolbar,1,0);
 lv_obj_set_style_pad_all(toolbar,4,0);
 lv_obj_set_style_pad_column(toolbar,4,0);
 lv_obj_set_flex_flow(toolbar,LV_FLEX_FLOW_ROW);
 picker=lv_dropdown_create(toolbar);
 lv_dropdown_set_options(picker,
  "Waveshare 720x720\nTab5 1280x720\nRound 480\nRound 360\nUltrawide 2560\nPortrait 480x800\nCompact 800x480");
 lv_obj_set_width(picker,179);
 lv_dropdown_set_selected(picker,selected);
 lv_obj_add_event_cb(picker,picker_changed,LV_EVENT_VALUE_CHANGED,nullptr);
 pause_button=lv_button_create(toolbar);
 lv_obj_set_size(pause_button,66,36);
 lv_obj_add_event_cb(pause_button,paused_clicked,LV_EVENT_CLICKED,nullptr);
 auto* pl=lv_label_create(pause_button);lv_label_set_text(pl,"PAUSE");lv_obj_center(pl);
 auto* label=lv_label_create(toolbar);
 lv_label_set_text(label,"SIM");
 lv_obj_set_style_text_color(label,lv_color_hex(0x55ff00),0);
 lv_obj_set_style_pad_top(label,10,0);
 lv_obj_move_foreground(toolbar);
}
bool change_preset(int i){
 if(i<0 || i>=static_cast<int>(std::size(presets)))return false;
 const auto& p=presets[i];
 Display metrics{p.w,p.h,p.shape};
 if(!plan(metrics).valid)return false;
 if(display){
  lv_display_set_resolution(display,p.w,p.h);
  lv_sdl_window_set_zoom(display,p.zoom);
  lv_sdl_window_set_title(display,p.title);
 }
 const bool ok=home.geometry().valid?home.resize(metrics):home.create(lv_screen_active(),metrics,action,nullptr);
 if(ok){
  selected=i;
  data.span_hz=span;
  home.update(data);
  if(toolbar)lv_obj_move_foreground(toolbar);
  if(picker)lv_dropdown_set_selected(picker,selected);
  std::printf("orcui_preset=%s form=%s %dx%d\n",p.key,form_name(home.geometry().form),p.w,p.h);
  std::fflush(stdout);
 }
 return ok;
}
void synthetic_fft(uint64_t frame){
 float phase=static_cast<float>(frame)*.032f;
 float center=268.f+18.f*std::sin(phase*.13f);
 for(unsigned i=0;i<fft.size();++i){
   float x=static_cast<float>(i);
   float a=55.f*std::exp(-std::pow((x-center)/3.f,2));
   float b=27.f*std::exp(-std::pow((x-106.f)/5.f,2));
   float c=19.f*std::exp(-std::pow((x-380.f-17.f*std::sin(phase*.25f))/7.f,2));
   fft[i]=-106.f+a+b+c+2.8f*std::sin(x*.73f+phase);
 }
}
void usage(const char* cmd){
 std::printf("Usage: %s [--preset waveshare|tab5|round480|round360|ultrawide|portrait|compact] [--seconds N] [--cycle-ms N] [--paused]\n",cmd);
}
}
int main(int argc,char** argv){
 int seconds=0,cycle_ms=0;
 const char* screenshot=nullptr;
 for(int i=1;i<argc;++i){
  if(!std::strcmp(argv[i],"--help")){usage(argv[0]);return 0;}
  if(!std::strcmp(argv[i],"--preset") && i+1<argc){
    bool found=false; const char* name=argv[++i];
    for(unsigned j=0;j<std::size(presets);++j)
       if(!std::strcmp(name,presets[j].key)){selected=j;found=true;break;}
    if(!found){usage(argv[0]);return 2;}
  }else if(!std::strcmp(argv[i],"--seconds") && i+1<argc){
    seconds=std::atoi(argv[++i]);if(seconds<0||seconds>86400)return 2;
  }else if(!std::strcmp(argv[i],"--cycle-ms") && i+1<argc){
    cycle_ms=std::atoi(argv[++i]);if(cycle_ms<100)return 2;
  }else if(!std::strcmp(argv[i],"--paused"))paused=true;
  else if(!std::strcmp(argv[i],"--screenshot") && i+1<argc) screenshot=argv[++i];
  else {usage(argv[0]);return 2;}
 }
 lv_init();
 auto& p=presets[selected];
 display=lv_sdl_window_create(p.w,p.h);
 if(!display){std::fprintf(stderr,"SDL display initialization failed\n");return 1;}
 lv_display_set_default(display);
 lv_sdl_window_set_zoom(display,p.zoom);
 lv_sdl_window_set_title(display,"OrcUI Simulator");
 lv_sdl_window_set_resizeable(display,false);
 if(auto* mouse=lv_sdl_mouse_create())lv_indev_set_display(mouse,display);
 data.frequency_hz=152007000;data.mode="SIM IQ";
 data.radio_ready=false;data.wifi_ready=false;data.battery_percent=-1;
 if(!change_preset(selected))return 1;
 create_toolbar();
 using clock=std::chrono::steady_clock;
 auto start=clock::now(),last=start,report=start,cycle=start;
 uint64_t frames=0;
 while(true){
  lv_timer_handler(); // SDL events are owned by LVGL's SDL backend.
  auto now=clock::now();
  if(pending>=0){int dest=pending;pending=-1;if(!change_preset(dest))return 1;}
  if(cycle_ms>0 && std::chrono::duration_cast<std::chrono::milliseconds>(now-cycle).count()>=cycle_ms){
    cycle=now;
    if(!change_preset((selected+1)%static_cast<int>(std::size(presets))))return 1;
  }
  if(!paused && now-last>=std::chrono::milliseconds(50)){
   last=now;synthetic_fft(frames++);
   home.spectrum(fft.data(),fft.size(),-120.f,-35.f);
   home.waterfall_line(fft.data(),fft.size(),-120.f,-35.f);
   data.span_hz=span;home.update(data);
  }
  if(now-report>=std::chrono::seconds(5)){
   report=now;
   std::printf("orcui_frames=%llu profile=%s touches=%u\n",
     static_cast<unsigned long long>(frames),presets[selected].key,touches);
   std::fflush(stdout);
  }
  if(seconds>0 && now-start>=std::chrono::seconds(seconds))break;
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
 // Optional screenshot of the actual rendered LVGL+SDL window.
 // SDL captures are read back before destroying display objects.
 if(screenshot){
  lv_timer_handler();
  auto* renderer=static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(display));
  SDL_Surface* shot=SDL_CreateRGBSurfaceWithFormat(0,
      lv_display_get_horizontal_resolution(display),
      lv_display_get_vertical_resolution(display),
      32,SDL_PIXELFORMAT_ARGB8888);
  if(!renderer || !shot ||
     SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,
                          shot->pixels,shot->pitch)!=0 ||
     SDL_SaveBMP(shot,screenshot)!=0){
    std::fprintf(stderr,"OrcUI screenshot failed: %s\n",SDL_GetError());
    if(shot)SDL_FreeSurface(shot);
    home.destroy();lv_deinit();return 1;
  }
  std::printf("orcui_screenshot=%s\n",screenshot);
  SDL_FreeSurface(shot);
 }
 home.destroy();lv_deinit();return 0;
}
