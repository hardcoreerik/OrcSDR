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
  const auto release=options_.release?options_.release:lv_free;
  if(spectrum_pixels_)release(spectrum_pixels_);
  if(waterfall_pixels_)release(waterfall_pixels_);
  if(badge_pixels_)release(badge_pixels_);
  badge_pixels_=nullptr;
  brand_=title_=status_=frequency_=spectrum_panel_=waterfall_panel_=nullptr;
  scrim_=nullptr; menu_open_=false;
  spectrum_pixels_=nullptr; waterfall_pixels_=nullptr;
  spectrum_canvas_=nullptr; waterfall_canvas_=nullptr;
  parent_=nullptr; menu_=nullptr; spectrum_stride_=waterfall_stride_=0;
  button_count_=0;
}
bool HomeLvgl::allocate_canvas(lv_obj_t* parent,lv_obj_t** canvas,
                               void** pixels,size_t* stride,const Rect& rect){
  constexpr int pad=5;
  const int w=rect.w-2*pad,h=rect.h-24;
  if(w<24 || h<24)return false;
  *stride=lv_draw_buf_width_to_stride(w,LV_COLOR_FORMAT_RGB565);
  const size_t size=*stride*static_cast<size_t>(h);
  *pixels=options_.allocate?options_.allocate(size):lv_malloc(size);  // Board allocator policy must put large buffers in PSRAM.
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
  if(!enabled(cmd,options_.capabilities))return;
  if(cmd==HomeCommand::open_menu)set_menu_open(!menu_open_);
  if(cmd==HomeCommand::close_menu || cmd==HomeCommand::fm_preview)set_menu_open(false);
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
  const bool active=enabled(cmd,options_.capabilities);
  lv_obj_set_style_text_color(label,lv_color_hex(active?kGreen:0x68747a),0);
  if(!active)lv_obj_add_state(button,LV_STATE_DISABLED);
  lv_obj_center(label);
}
bool HomeLvgl::create(lv_obj_t* parent,const Display& display,ActionSink sink,void* context,const HomeOptions& options){
  destroy();
  options_=options;
  if(bool(options.allocate)!=bool(options.release))return false;
  layout_=plan(display);
  if(!parent || !layout_.valid)return false;
  parent_=parent; display_=display; sink_=sink; context_=context;
  root_=lv_obj_create(parent);
  place(root_,layout_.safe,{0,0,0,0});
  style_box(root_,kBg);
  lv_obj_set_style_border_width(root_,0,0);
  lv_obj_set_style_bg_color(root_,lv_color_hex(kBg),0);
  panel(&title_,layout_.header,"",false);
  brand_=lv_label_create(title_);
  lv_obj_t* brand=brand_;
  lv_label_set_text(brand,"OrcSDR / HOME  MENU >");
  label_style(brand,kWhite,layout_.menu_toggle.w+9,layout_.header.h/2-9);
  lv_obj_set_width(brand,layout_.header.w-layout_.menu_toggle.w-12);
  lv_label_set_long_mode(brand,LV_LABEL_LONG_CLIP);
  add_button(root_,layout_.menu_toggle,options_.badge_le565?"":"MENU",HomeCommand::open_menu,0);
  if(options_.badge_le565 && options_.badge_size>0) {
    const int n=std::min(72,std::min(layout_.menu_toggle.w,layout_.menu_toggle.h)-4);
    const size_t stride=lv_draw_buf_width_to_stride(n,LV_COLOR_FORMAT_RGB565);
    badge_pixels_=options_.allocate?options_.allocate(stride*n):lv_malloc(stride*n);
    if(!badge_pixels_){destroy();return false;}
    for(int y=0;y<n;++y)for(int x=0;x<n;++x){
      const int i=2*((y*options_.badge_size/n)*options_.badge_size+x*options_.badge_size/n);
      auto* row=reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(badge_pixels_)+y*stride);
      row[x]=uint16_t(options_.badge_le565[i])|uint16_t(options_.badge_le565[i+1]<<8);
    }
    auto* badge=lv_canvas_create(root_);
    lv_canvas_set_buffer(badge,badge_pixels_,n,n,LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(badge,layout_.menu_toggle.x-layout_.safe.x+(layout_.menu_toggle.w-n)/2,
                     layout_.menu_toggle.y-layout_.safe.y+(layout_.menu_toggle.h-n)/2);
    lv_obj_remove_flag(badge,LV_OBJ_FLAG_CLICKABLE);
  }
  if(layout_.show_sidebar){
    lv_obj_t* sidebar=nullptr;
    panel(&sidebar,layout_.sidebar,"DASHBOARDS",true);
    lv_obj_t* nav=lv_label_create(sidebar);
    lv_label_set_text(nav,"HOME\n\nFM IQ unavailable\nP25 unavailable\nADS-B unavailable\nRF LAB unavailable");
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
#if LV_FONT_MONTSERRAT_28
  if(layout_.frequency.w>=500)lv_obj_set_style_text_font(frequency_,&lv_font_montserrat_28,0);
#endif
  lv_obj_set_width(frequency_,layout_.frequency.w-24);
  lv_label_set_long_mode(frequency_,LV_LABEL_LONG_CLIP);
  label_style(frequency_,kGreen,12,4);
  if(layout_.show_controls){
    const char* captions[]={"TUNE -","STEP","TUNE +","PALETTE","FM IQ (N/A)"};
    const HomeCommand actions[]={HomeCommand::tune_down,HomeCommand::cycle_step,
                                 HomeCommand::tune_up,HomeCommand::cycle_palette,HomeCommand::fm_preview};
    for(int i=0;i<5;++i)add_button(root_,layout_.control_buttons[i],captions[i],actions[i],i);
  }
  if(layout_.status.valid()){
    lv_obj_t* status_panel=nullptr;
    panel(&status_panel,layout_.status,"",false);
    status_=lv_label_create(status_panel);
    label_style(status_,kWhite,8,2);
    lv_obj_set_width(status_,layout_.status.w-16);
    lv_label_set_long_mode(status_,LV_LABEL_LONG_CLIP);
  }
  // Scrim is above every underlying input target; header remains available to toggle.
  scrim_=lv_obj_create(root_);
  const Rect shield{layout_.safe.x,layout_.header.bottom(),layout_.safe.w,
                    layout_.safe.bottom()-layout_.header.bottom()};
  place(scrim_,shield,layout_.safe);
  style_box(scrim_,kBg);
  lv_obj_add_flag(scrim_,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(scrim_,LV_OPA_50,0);
  lv_obj_set_style_border_width(scrim_,0,0);
  buttons_[button_count_]={this,HomeCommand::close_menu};
  lv_obj_add_event_cb(scrim_,event_cb,LV_EVENT_CLICKED,&buttons_[button_count_++]);
  panel(&menu_,layout_.menu_overlay,"LAST USED",true);
  lv_obj_add_flag(menu_,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(menu_,LV_DIR_VER);
  const char* names[]={"HOME","P25 (unavailable)","AM (unavailable)",
    "SETTINGS (unavailable)","RF LAB (unavailable)","FM IQ (N/A)",
    "ADS-B (unavailable)","ALL (unavailable)"};
  for(int i=0;i<8;++i)
    add_button(menu_,menu_row(layout_,i),names[i],
      i==0?HomeCommand::close_menu:i==5?HomeCommand::fm_preview:HomeCommand::mode,7+i);
  // Compact round displays reach the same Home actions through the scrolling drawer.
  if(!layout_.show_controls){
    const char* captions[]={"TUNE -","STEP","TUNE +","PALETTE"};
    const HomeCommand actions[]={HomeCommand::tune_down,HomeCommand::cycle_step,
      HomeCommand::tune_up,HomeCommand::cycle_palette};
    for(int i=0;i<4;++i)add_button(menu_,menu_row(layout_,8+i),captions[i],actions[i],i);
  }
  set_menu_open(false);
  update(data_);
  return true;
}
bool HomeLvgl::resize(const Display& display){
  if(!parent_)return false;
  const auto sink=sink_;void* ctx=context_;lv_obj_t* parent=parent_;
  const auto options=options_;
  return create(parent,display,sink,ctx,options);
}
void HomeLvgl::set_visible(bool visible){
  if(!root_)return;
  if(visible){lv_obj_remove_flag(root_,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(root_);}
  else lv_obj_add_flag(root_,LV_OBJ_FLAG_HIDDEN);
}
void HomeLvgl::set_menu_open(bool open){
  menu_open_=open;
  if(brand_)lv_label_set_text(brand_,open?"OrcSDR / HOME  MENU <":"OrcSDR / HOME  MENU >");
  if(!menu_)return;
  if(open){lv_obj_remove_flag(scrim_,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(menu_,LV_OBJ_FLAG_HIDDEN);}
  else {lv_obj_add_flag(menu_,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(scrim_,LV_OBJ_FLAG_HIDDEN);}
}
void HomeLvgl::update(const HomeData& data){
  data_=data;
  if(frequency_){
    char buf[80];
    std::snprintf(buf,sizeof(buf),"%.6f MHz  %s\nSTEP %lu Hz  %s",data.frequency_hz/1000000.0,
                  data.mode?data.mode:"--",static_cast<unsigned long>(data.step_hz),
                  data.palette_name?data.palette_name:"--");
    lv_label_set_text(frequency_,buf);
  }
  if(status_){
    char buf[90];
    std::snprintf(buf,sizeof(buf),"%s %lu ks/s drops %lu | %lus\nAPPLIED %lu Hz | Wi-Fi/audio N/A",
        data.radio_ready?"IQ ACTIVE":"NO IQ",static_cast<unsigned long>(data.effective_sps/1000),
        static_cast<unsigned long>(data.drops),static_cast<unsigned long>(data.uptime_sec),
        static_cast<unsigned long>(data.applied_hz));
    lv_label_set_text(status_,buf);
  }
}
void HomeLvgl::clear_plots(){
  if(spectrum_canvas_){
    std::memset(spectrum_pixels_,0,spectrum_stride_*lv_obj_get_height(spectrum_canvas_));
    lv_obj_invalidate(spectrum_canvas_);
  }
  if(waterfall_canvas_){
    std::memset(waterfall_pixels_,0,waterfall_stride_*lv_obj_get_height(waterfall_canvas_));
    lv_obj_invalidate(waterfall_canvas_);
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
    db,count,floor,ceiling,options_.palette256);
  lv_obj_invalidate(waterfall_canvas_);
}
}
