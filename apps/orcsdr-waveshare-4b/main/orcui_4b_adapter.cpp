#include "orcui_4b_adapter.hpp"
#include "orcui_command_map.hpp"
#include "orcui/home_lvgl.hpp"
#include "waterfall_style.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <array>
namespace orcsdr::waveshare4b::adaptive {
namespace {
orcui::HomeLvgl view;
QueueHandle_t commands=nullptr;
std::array<uint16_t,256> colors{};
extern const uint8_t badge[] asm("_binary_orc_badge_104_rgb565_start");
extern const uint8_t badge_end[] asm("_binary_orc_badge_104_rgb565_end");
void* allocate(size_t bytes){return heap_caps_aligned_alloc(64,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
void palette(){
 for(unsigned i=0;i<colors.size();++i)
  colors[i]=waterfall_style::color565(waterfall_style::Screen::home,float(i)/255.f);
}
void action(orcui::HomeCommand c,void*) {
 const auto a=radio_action(c);
 if(a!=RadioAction::none){
  const home_v3::Command cmd=a==RadioAction::down?home_v3::Command::Down:
    a==RadioAction::up?home_v3::Command::Up:home_v3::Command::Step;
  if(xQueueSend(commands,&cmd,0)!=pdTRUE)ESP_LOGW("ORCUI_4B","command queue full");
 }else if(c==orcui::HomeCommand::cycle_palette){
  waterfall_style::next_palette(waterfall_style::Screen::home);palette();
 }
 ESP_LOGI("ORCUI_4B","action=%u drawer=%u",unsigned(c),unsigned(view.menu_open()));
}
}
bool create(QueueHandle_t queue) {
 commands=queue;palette();
 lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0),0);
 auto* display=lv_display_get_default();
 if(!display||!queue||badge_end-badge!=104*104*2)return false;
 orcui::HomeOptions options{};
 options.capabilities={true,true,true,false}; // FM stays unavailable until separately qualified.
 options.allocate=allocate;options.release=heap_caps_free;
 options.badge_be565=badge;options.badge_size=104;options.palette256=colors.data();
 const orcui::Display profile{lv_display_get_horizontal_resolution(display),
                            lv_display_get_vertical_resolution(display)};
 ESP_LOGI("ORCUI_4B","adaptive panel=%dx%d PSRAM canvases",profile.width,profile.height);
 return view.create(lv_screen_active(),profile,action,nullptr,options);
}
void update(const home_v3::Snapshot& s,const uint8_t* bins,bool fresh) {
 // Called only by the LVGL timer; radio task publishes bounded snapshots, never draws.
 const int64_t start=esp_timer_get_time();
 orcui::HomeData data{};
 data.frequency_hz=s.requested_hz;data.applied_hz=s.applied_hz;
 data.step_hz=s.step_hz;data.span_hz=960000;data.effective_sps=s.effective_sps;
 data.drops=s.drops;data.uptime_sec=s.uptime_sec;data.receiver_state=s.receiver_state;
 data.radio_ready=s.receiver_state==2;
 data.palette_name=waterfall_style::palette_name(waterfall_style::palette(waterfall_style::Screen::home));
 view.update(data);
 if(data.radio_ready&&fresh){
  std::array<float,256> relative{};
  for(unsigned i=0;i<relative.size();++i)relative[i]=bins[i];
  // Existing FFT bytes are relative intensity, not calibrated dBFS/dBm.
  view.spectrum(relative.data(),relative.size(),0,255);
  view.waterfall_line(relative.data(),relative.size(),0,255);
 }
 static bool had_rf=false;
 if(!data.radio_ready&&had_rf)view.clear_plots();
 had_rf=data.radio_ready;
 static int64_t report=0;
 static std::array<int64_t,40> times{};
 static unsigned samples=0;
 times[samples++%times.size()]=esp_timer_get_time()-start;
 if(start-report>=5000000){
  auto sorted=times;
  const unsigned n=std::min<unsigned>(samples,times.size());
  std::sort(sorted.begin(),sorted.begin()+n);
  ESP_LOGI("ORCUI_4B","render_samples=%u p50_us=%lld p95_us=%lld dma_min=%u psram_free=%u",
   n,(long long)sorted[n/2],(long long)sorted[(n-1)*95/100],
   unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)),
   unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  report=start;
 }
}
}
