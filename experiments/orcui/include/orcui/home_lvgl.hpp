#pragma once
#include "orcui/layout.hpp"
#include "orcui/home_model.hpp"
#include <cstddef>
#include <cstdint>
#include <lvgl.h>
namespace orcsdr::orcui {
using ActionSink=void (*)(HomeCommand command,void* context);
struct HomeData {
  uint32_t frequency_hz=152007000;
  uint32_t step_hz=12500;
  uint32_t span_hz=960000;
  int battery_percent=-1;
  uint32_t applied_hz=0,effective_sps=0,drops=0,uptime_sec=0;
  int receiver_state=0;
  const char* palette_name="--";
  bool radio_ready=false,wifi_ready=false;
  const char* mode="IQ ONLY";
};
struct HomeOptions {
  HomeCapabilities capabilities{};
  void* (*allocate)(size_t)=nullptr;
  void (*release)(void*)=nullptr;
  const uint8_t* badge_be565=nullptr;
  int badge_size=0;
  const uint16_t* palette256=nullptr;
};
class HomeLvgl {
public:
  HomeLvgl()=default;
  ~HomeLvgl();
  HomeLvgl(const HomeLvgl&)=delete;
  HomeLvgl& operator=(const HomeLvgl&)=delete;
  bool create(lv_obj_t* parent,const Display& display,
              ActionSink sink=nullptr,void* context=nullptr,
              const HomeOptions& options={});
  bool resize(const Display& display);
  void update(const HomeData& data);
  void spectrum(const float* db,size_t count,float floor_db,float ceiling_db);
  void waterfall_line(const float* db,size_t count,float floor_db,float ceiling_db);
  void set_menu_open(bool open);
  void set_visible(bool visible);
  void clear_plots();
  bool menu_open() const {return menu_open_;}
  const void* spectrum_buffer() const {return spectrum_pixels_;}
  const void* waterfall_buffer() const {return waterfall_pixels_;}
  const Layout& geometry() const {return layout_;}
  void destroy();
private:
  static void event_cb(lv_event_t* event);
  void command(HomeCommand cmd);
  void add_button(lv_obj_t* parent,const Rect& rect,const char* label,
                  HomeCommand cmd,int slot);
  void panel(lv_obj_t** dest,const Rect& r,const char* title,bool label);
  bool allocate_canvas(lv_obj_t* panel,lv_obj_t** canvas,void** pixels,
                       size_t* stride,const Rect& rect);
  lv_obj_t* parent_=nullptr;
  lv_obj_t* root_=nullptr;
  lv_obj_t* title_=nullptr;
  lv_obj_t* brand_=nullptr;
  lv_obj_t* status_=nullptr;
  lv_obj_t* frequency_=nullptr;
  lv_obj_t* spectrum_panel_=nullptr;
  lv_obj_t* waterfall_panel_=nullptr;
  lv_obj_t* spectrum_canvas_=nullptr;
  lv_obj_t* waterfall_canvas_=nullptr;
  lv_obj_t* menu_=nullptr;
  lv_obj_t* scrim_=nullptr;
  void* badge_pixels_=nullptr;
  HomeOptions options_{};
  void* spectrum_pixels_=nullptr;
  void* waterfall_pixels_=nullptr;
  size_t spectrum_stride_=0,waterfall_stride_=0;
  struct ButtonState {
    HomeLvgl* owner=nullptr;
    HomeCommand command=HomeCommand::open_menu;
  } buttons_[16]{};
  int button_count_=0;
  Layout layout_{};
  Display display_{};
  HomeData data_{};
  ActionSink sink_=nullptr;
  void* context_=nullptr;
  bool menu_open_=false;
};
}