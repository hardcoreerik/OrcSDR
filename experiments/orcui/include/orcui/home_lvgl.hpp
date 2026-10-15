#pragma once
#include "orcui/layout.hpp"
#include <cstddef>
#include <cstdint>
#include <lvgl.h>
namespace orcsdr::orcui {
enum class HomeCommand : uint8_t {
  open_menu, close_menu, span_down, span_up, step_down, step_up,
  mode, filter, gain, frequency_entry
};
using ActionSink=void (*)(HomeCommand command,void* context);
struct HomeData {
  uint32_t frequency_hz=152007000;
  uint32_t step_hz=12500;
  uint32_t span_hz=960000;
  int battery_percent=98;
  bool radio_ready=false,wifi_ready=false;
  const char* mode="NFM";
};
class HomeLvgl {
public:
  HomeLvgl()=default;
  ~HomeLvgl();
  HomeLvgl(const HomeLvgl&)=delete;
  HomeLvgl& operator=(const HomeLvgl&)=delete;
  bool create(lv_obj_t* parent,const Display& display,
              ActionSink sink=nullptr,void* context=nullptr);
  bool resize(const Display& display);
  void update(const HomeData& data);
  void spectrum(const float* db,size_t count,float floor_db,float ceiling_db);
  void waterfall_line(const float* db,size_t count,float floor_db,float ceiling_db);
  void set_menu_open(bool open);
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
  lv_obj_t* status_=nullptr;
  lv_obj_t* frequency_=nullptr;
  lv_obj_t* spectrum_panel_=nullptr;
  lv_obj_t* waterfall_panel_=nullptr;
  lv_obj_t* spectrum_canvas_=nullptr;
  lv_obj_t* waterfall_canvas_=nullptr;
  lv_obj_t* menu_=nullptr;
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