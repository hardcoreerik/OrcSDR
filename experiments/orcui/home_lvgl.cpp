#include "orcui/home_lvgl.hpp"
#include "orcui/raster.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace orcsdr::orcui {
namespace {
constexpr uint32_t kBg=0x000000,kPanel=0x00131a,kCyan=0x00cfff,kGreen=0x55ff00,kWhite=0xe9faff;
void style_box(lv_obj_t* o,uint32_t border=kCyan){
  lv_obj_set_style_bg_color(o,lv_color_hex(kPanel),0);
  lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
  lv_obj_set_style_border_color(o,lv_color_hex(border),0);
  lv_obj_set_style_border_width(o,1,0);
  lv_obj_set_style_radius(o,6,0);
  lv_obj_set_style_pad_all(o,0,0);
  lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
}
void place(lv_obj_t* o,const Rect& r,const Rect& origin){
  lv_obj_set_pos(o,r.x-origin.x,r.y-origin.y);
  lv_obj_set_size(o,r.w,r.h);
}
void label_style(lv_obj_t* label,uint32_t color,int x,int y){
  lv_obj_set_style_text_color(label,lv_color_hex(color),0);
  lv_obj_set_pos(label,x,y);
}
}
HomeLvgl::~HomeLvgl(){destroy();}
void HomeLvgl::destroy(){
  // UI thread only. Caller waits for in-flight LVGL flushes before teardown.
  if(root_)lv_obj_delete(root_);
  root_=nullptr;
  // All child pointers are owned by root_. A shape/profile switch can omit
  // status or other widgets, so leaving old pointers here is use-after-free.
  title_=nullptr;
  status_=nullptr;
  frequency_=nullptr;
  spectrum_panel_=nullptr;
  waterfall_panel_=nullptr;
  if(spectrum_pixels_)lv_free(spectrum_pixels_);
  if(waterfall_pixels_)lv_free(waterfall_pixels_);
  spectrum_pixels_=nullptr; waterfall_pixels_=nullptr;
  spectrum_canvas_=nullptr; waterfall_canvas_=nullptr;
  parent_=nullptr; menu_=nullptr; spectrum_stride_=waterfall_stride_=0;
  button_count_=0;
  layout_={};
  last_frequency_label_[0]=0;
  last_status_label_[0]=0;
}
bool HomeLvgl::allocate_canvas(lv_obj_t* parent,lv_obj_t** canvas,
                               void** pixels,size_t* stride,const Rect& rect){
  constexpr int pad=5;
  const int w=rect.w-2*pad,h=rect.h-24;
  if(w<24 || h<24)return false;
  *stride=lv_draw_buf_width_to_stride(w,LV_COLOR_FORMAT_RGB565);
  const size_t size=*stride*static_cast<size_t>(h);
  *pixels=lv_malloc(size);  // Board allocator policy must put large buffers in PSRAM.
  if(!*pixels)return false;
  std::memset(*pixels,0,size);
  *canvas=lv_canvas_create(parent);
  lv_canvas_set_buffer(*canvas,*pixels,w,h,LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(*canvas,pad,20);
  lv_obj_set_size(*canvas,w,h);
  return true;
}
void HomeLvgl::panel(lv_obj_t** dest,const Rect& rect,const char* title,bool with_label){
  *dest=lv_obj_create(root_);
  place(*dest,rect,layout_.safe);
  style_box(*dest);
  if(with_label){
    lv_obj_t* text=lv_label_create(*dest);
    lv_label_set_text(text,title);
    label_style(text,kCyan,8,3);
  }
}
void HomeLvgl::event_cb(lv_event_t* e){
  if(lv_event_get_code(e)!=LV_EVENT_CLICKED)return;
  auto* state=static_cast<ButtonState*>(lv_event_get_user_data(e));
  if(state && state->owner)state->owner->command(state->command);
}
void HomeLvgl::command(HomeCommand cmd){
  if(cmd==HomeCommand::open_menu)set_menu_open(true);
  if(cmd==HomeCommand::close_menu)set_menu_open(false);
  if(sink_)sink_(cmd,context_);
}
void HomeLvgl::add_button(lv_obj_t* parent,const Rect& rect,const char* text,
                          HomeCommand cmd,int){
  if(button_count_>=16 || !rect.valid())return;
  const Rect origin=parent==root_?layout_.safe:
    parent==menu_?layout_.menu_overlay:layout_.controls;
  lv_obj_t* button=lv_button_create(parent);
  place(button,rect,origin);
  style_box(button,kCyan);
  ButtonState& state=buttons_[button_count_++];
  state={this,cmd};
  lv_obj_add_event_cb(button,event_cb,LV_EVENT_CLICKED,&state);
  lv_obj_t* label=lv_label_create(button);
  lv_label_set_text(label,text);
  lv_obj_set_style_text_color(label,lv_color_hex(kGreen),0);
  lv_obj_center(label);
}
bool HomeLvgl::create(lv_obj_t* parent,const Display& display,ActionSink sink,void* context){
  destroy();
  layout_=plan(display);
  if(!parent || !layout_.valid)return false;
  parent_=parent; display_=display; sink_=sink; context_=context;
  root_=lv_obj_create(parent);
  place(root_,layout_.safe,{0,0,0,0});
  style_box(root_,kBg);
  lv_obj_set_style_bg_color(root_,lv_color_hex(kBg),0);
  panel(&title_,layout_.header,"",false);
  lv_obj_t* brand=lv_label_create(title_);
  lv_label_set_text(brand,"OrcSDR / HOME");
  label_style(brand,kWhite,layout_.menu_toggle.w+9,layout_.header.h/2-9);
  add_button(root_,layout_.menu_toggle,"MENU",HomeCommand::open_menu,0);
  if(layout_.show_sidebar){
    lv_obj_t* sidebar=nullptr;
    panel(&sidebar,layout_.sidebar,"DASHBOARDS",true);
    lv_obj_t* nav=lv_label_create(sidebar);
    lv_label_set_text(nav,"HOME\n\nFM RADIO\n\nP25\n\nADS-B\n\nRF LAB");
    label_style(nav,kWhite,15,38);
  }
  if(layout_.show_inspector){
    lv_obj_t* pane=nullptr;
    panel(&pane,layout_.inspector,"SIGNAL DETAILS",true);
  }
  panel(&spectrum_panel_,layout_.spectrum,"SPECTRUM",true);
  panel(&waterfall_panel_,layout_.waterfall,"WATERFALL",true);
  if(!allocate_canvas(spectrum_panel_,&spectrum_canvas_,&spectrum_pixels_,&spectrum_stride_,layout_.spectrum) ||
     !allocate_canvas(waterfall_panel_,&waterfall_canvas_,&waterfall_pixels_,&waterfall_stride_,layout_.waterfall)){
    destroy();return false;
  }
  lv_obj_t* freq_panel=nullptr;
  panel(&freq_panel,layout_.frequency,"",false);
  frequency_=lv_label_create(freq_panel);
  label_style(frequency_,kGreen,12,layout_.frequency.h/2-13);
  if(layout_.show_controls){
    const char* captions[]={"SPAN -","SPAN +","MODE","FILTER","GAIN"};
    const HomeCommand actions[]={HomeCommand::span_down,HomeCommand::span_up,
                                 HomeCommand::mode,HomeCommand::filter,HomeCommand::gain};
    for(int i=0;i<5;++i)add_button(root_,layout_.control_buttons[i],captions[i],actions[i],i);
  }
  if(layout_.status.valid()){
    lv_obj_t* status_panel=nullptr;
    panel(&status_panel,layout_.status,"",false);
    status_=lv_label_create(status_panel);
    label_style(status_,kWhite,8,layout_.status.h/2-8);
  }
  panel(&menu_,layout_.menu_overlay,"DASHBOARDS",true);
  const int x=layout_.menu_overlay.x+8,y=layout_.menu_overlay.y+34,w=layout_.menu_overlay.w-16;
  const int h=std::max(24,std::min(45,(layout_.menu_overlay.h-50)/4));
  for(int i=0;i<3;++i)
    add_button(menu_,{x,y+i*(h+8),w,h},
      i==0?"HOME":i==1?"FM RADIO":"ALL DASHBOARDS",HomeCommand::close_menu,7+i);
  set_menu_open(false);
  update(data_);
  return true;
}
bool HomeLvgl::resize(const Display& display){
  if(!parent_)return false;
  const auto sink=sink_;void* ctx=context_;lv_obj_t* parent=parent_;
  const bool previously_open=menu_open_;
  const bool ok=create(parent,display,sink,ctx);
  if(ok)set_menu_open(previously_open);
  return ok;
}
void HomeLvgl::set_menu_open(bool open){
  menu_open_=open;
  if(!menu_)return;
  if(open)lv_obj_remove_flag(menu_,LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(menu_,LV_OBJ_FLAG_HIDDEN);
}
void HomeLvgl::update(const HomeData& data){
  data_=data;
  if(frequency_){
    char buf[80];
    std::snprintf(buf,sizeof(buf),"%.3f MHz  %s",data.frequency_hz/1000000.0,
                  data.mode?data.mode:"--");
    if(std::strcmp(buf,last_frequency_label_)!=0){
      lv_label_set_text(frequency_,buf);
      std::snprintf(last_frequency_label_,sizeof(last_frequency_label_),"%s",buf);
    }
  }
  if(status_){
    char buf[90];
    std::snprintf(buf,sizeof(buf),"RTL %s  |  WIFI %s  |  %d%%",
        data.radio_ready?"READY":"OFF",data.wifi_ready?"ON":"OFF",data.battery_percent);
    if(std::strcmp(buf,last_status_label_)!=0){
      lv_label_set_text(status_,buf);
      std::snprintf(last_status_label_,sizeof(last_status_label_),"%s",buf);
    }
  }
}
void HomeLvgl::spectrum(const float* db,size_t count,float floor,float ceiling){
  if(!spectrum_canvas_)return;
  paint_spectrum({static_cast<uint16_t*>(spectrum_pixels_),
    lv_obj_get_width(spectrum_canvas_),lv_obj_get_height(spectrum_canvas_),spectrum_stride_},
    db,count,floor,ceiling);
  lv_obj_invalidate(spectrum_canvas_);
}
void HomeLvgl::waterfall_line(const float* db,size_t count,float floor,float ceiling){
  if(!waterfall_canvas_)return;
  push_waterfall({static_cast<uint16_t*>(waterfall_pixels_),
    lv_obj_get_width(waterfall_canvas_),lv_obj_get_height(waterfall_canvas_),waterfall_stride_},
    db,count,floor,ceiling);
  lv_obj_invalidate(waterfall_canvas_);
}
}