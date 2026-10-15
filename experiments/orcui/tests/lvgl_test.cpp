#include "orcui/home_lvgl.hpp"
#include "orcui_command_map.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
using namespace orcsdr::orcui;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
namespace {
unsigned allocations=0,releases=0,down=0,up=0,steps=0,palettes=0;
bool fail_allocate=false;
void* allocate(size_t n){if(fail_allocate)return nullptr;++allocations;return std::malloc(n);}
void release(void* p){++releases;std::free(p);}
void action(HomeCommand c,void*){
 using namespace orcsdr::waveshare4b::adaptive;
 if(radio_action(c)==RadioAction::down)++down;
 if(radio_action(c)==RadioAction::up)++up;
 if(radio_action(c)==RadioAction::step)++steps;
 if(c==HomeCommand::cycle_palette)++palettes;
}
lv_point_t point{};bool pressed=false;
void read(lv_indev_t*,lv_indev_data_t* d){d->point=point;d->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;}
void tap(lv_indev_t* input,int x,int y){
 point={x,y};pressed=true;lv_tick_inc(30);lv_indev_read(input);
 pressed=false;lv_tick_inc(30);lv_indev_read(input);
}
std::vector<uint16_t> frame;
void flush(lv_display_t* d,const lv_area_t* area,uint8_t* px){
 const int w=lv_display_get_horizontal_resolution(d),h=lv_display_get_vertical_resolution(d);
 frame.resize(size_t(w)*h);
 const size_t stride=lv_draw_buf_width_to_stride(area->x2-area->x1+1,LV_COLOR_FORMAT_RGB565);
 for(int y=area->y1;y<=area->y2;++y)
  std::memcpy(frame.data()+size_t(y)*w+area->x1,px+(y-area->y1)*stride,size_t(area->x2-area->x1+1)*2);
 lv_display_flush_ready(d);
}
lv_obj_t* find_button(lv_obj_t* root,const char* text){
 for(unsigned i=0;i<lv_obj_get_child_count(root);++i){
  auto* child=lv_obj_get_child(root,i);
  if(lv_obj_check_type(child,&lv_label_class)&&std::strcmp(lv_label_get_text(child),text)==0)
   return lv_obj_get_parent(child);
  if(auto* found=find_button(child,text))return found;
 }
 return nullptr;
}
void save_frame(const char* path,int w,int h){
 auto* f=std::fopen(path,"wb");CHECK(f);std::fprintf(f,"P6\n%d %d\n255\n",w,h);
 for(auto p:frame){const unsigned char rgb[]={static_cast<unsigned char>(((p>>11)&31)*255/31),
  static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};
  CHECK(std::fwrite(rgb,1,3,f)==3);}
 std::fclose(f);
}
}
int main(){
 lv_init();
 auto* display=lv_display_create(2560,1280);
 std::array<uint16_t,2560*16> pixels{};
 lv_display_set_buffers(display,pixels.data(),nullptr,pixels.size()*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(display,flush);
 auto* input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,read);
 HomeOptions options{};options.capabilities={true,true,true,false};options.allocate=allocate;options.release=release;
 std::ifstream badge_file(ORCUI_BADGE_PATH,std::ios::binary);
 std::vector<uint8_t> badge((std::istreambuf_iterator<char>(badge_file)),{});
 CHECK(badge.size()==104*104*2);options.badge_le565=badge.data();options.badge_size=104;
 HomeLvgl home;
 for(const Display profile: {Display{1280,720},Display{720,720},Display{480,480,Shape::circle},
      Display{360,360,Shape::circle},Display{480,800},Display{2560,720}}){
  lv_display_set_resolution(display,profile.width,profile.height);
  CHECK(home.create(lv_screen_active(),profile,action,nullptr,options));
  lv_obj_update_layout(lv_screen_active());
  const auto g=home.geometry();
  if(profile.width==720&&profile.height==720){
   HomeData sample{};sample.frequency_hz=96100000;sample.applied_hz=96100000;
   sample.radio_ready=true;sample.effective_sps=960000;sample.mode="IQ TEST";
   home.update(sample);lv_refr_now(display);
   unsigned green=0,red=0;
   for(int y=g.menu_toggle.y+2;y<g.menu_toggle.y+48;++y)
    for(int x=g.menu_toggle.x+2;x<g.menu_toggle.x+48;++x){
     auto pixel=frame[size_t(y)*720+x];
     if(((pixel>>5)&63)>32 && ((pixel>>5)&63)>2*((pixel>>11)&31)+6
        && ((pixel>>5)&63)>2*(pixel&31)+6)++green;
     if(((pixel>>11)&31)>20&&((pixel>>5)&63)<25)++red;
    }
   std::printf("badge green=%u red=%u\n",green,red);
   CHECK(green>100&&green>red*2);
   save_frame("host-synthetic-home.ppm",720,720);
  }
  const auto* a=home.spectrum_buffer();const auto* b=home.waterfall_buffer();
  const auto n=allocations;
  for(unsigned i=0;i<50;++i){
   tap(input,g.menu_toggle.x+10,g.menu_toggle.y+10);CHECK(home.menu_open());
   const auto before=down+up+steps;
   // On square/portrait this point lies over enabled tuning beneath the scrim.
   const Rect target=g.show_controls?g.control_buttons[2]:g.frequency;
   tap(input,target.right()-5,target.bottom()-5);
   CHECK(!home.menu_open());CHECK(before==down+up+steps);
   CHECK(home.spectrum_buffer()==a&&home.waterfall_buffer()==b&&allocations==n);
  }
  if(g.show_controls){
   auto click=[&](int i){const auto r=g.control_buttons[i];tap(input,r.x+r.w/2,r.y+r.h/2);};
   const auto d=down,u=up,s=steps,p=palettes;
   click(0);click(1);click(2);click(3);click(4);
   CHECK(down==d+1&&up==u+1&&steps==s+1&&palettes==p+1);
  }
  tap(input,g.menu_toggle.x+10,g.menu_toggle.y+10);CHECK(home.menu_open());
  auto* home_button=find_button(lv_screen_active(),"HOME");CHECK(home_button);
  lv_obj_scroll_to_view(home_button,LV_ANIM_OFF);lv_obj_update_layout(lv_screen_active());
  lv_area_t area{};lv_obj_get_coords(home_button,&area);
  tap(input,(area.x1+area.x2)/2,(area.y1+area.y2)/2);CHECK(!home.menu_open());
  if(!g.show_controls){
   const auto d=down,u=up,s=steps,p=palettes;
   tap(input,g.menu_toggle.x+10,g.menu_toggle.y+10);CHECK(home.menu_open());
   for(const char* name:{"TUNE -","STEP","TUNE +","PALETTE"}){
    auto* button=find_button(lv_screen_active(),name);CHECK(button);
    lv_obj_scroll_to_view(button,LV_ANIM_OFF);lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(button,&area);
    CHECK(area.x1>=g.safe.x && area.x2<g.safe.right());
    CHECK(area.y1>=g.menu_overlay.y && area.y2<g.menu_overlay.bottom());
    tap(input,(area.x1+area.x2)/2,(area.y1+area.y2)/2);
   }
   CHECK(down==d+1&&up==u+1&&steps==s+1&&palettes==p+1);
   tap(input,g.menu_toggle.x+10,g.menu_toggle.y+10);CHECK(!home.menu_open());
  }
  tap(input,g.menu_toggle.x+10,g.menu_toggle.y+10);CHECK(home.menu_open());
  tap(input,g.menu_toggle.x+10,g.menu_toggle.y+10);CHECK(!home.menu_open());
  std::puts(form_name(g.form));
 }
 // Rectangle -> round previously updated a freed status label.
 CHECK(home.resize({360,360,Shape::circle}));home.update({});
 fail_allocate=true;CHECK(!home.resize({720,720}));home.update({});
 fail_allocate=false;CHECK(home.create(lv_screen_active(),{720,720},action,nullptr,options));
 home.destroy();home.update({});CHECK(allocations==releases);
 lv_indev_delete(input);lv_display_delete(display);lv_deinit();
 std::puts("PASS LVGL lifecycle, allocation failure, modal input, action mapping and 300 dismissals");
}
