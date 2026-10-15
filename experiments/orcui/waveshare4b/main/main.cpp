// OrcUI display/touch experiment: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B.
// SYNTHETIC RF visuals only. No RTL-SDR access or C6 firmware modification.
#include <array>
#include <cmath>
#include <cstdint>

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orcui/home_lvgl.hpp"

namespace {
using orcsdr::orcui::Display;
using orcsdr::orcui::HomeCommand;
using orcsdr::orcui::HomeData;
using orcsdr::orcui::HomeLvgl;
using orcsdr::orcui::HomeOptions;
using orcsdr::orcui::Shape;
constexpr char kTag[] = "OrcUI-4B";
constexpr unsigned kBins = 256;
HomeLvgl g_home;
HomeData g_state{};
uint32_t g_span = 960000;
uint32_t g_touch_commands = 0;

void on_command(HomeCommand command, void*) {
  // LVGL callback: no receiver commands in this visual-only experiment.
  ++g_touch_commands;
  switch (command) {
    case HomeCommand::span_down:
      g_span = g_span > 240000 ? g_span / 2 : 120000; break;
    case HomeCommand::span_up:
      g_span = g_span < 2400000 ? g_span * 2 : 2400000; break;
    case HomeCommand::tune_down:
    case HomeCommand::step_down: g_state.frequency_hz -= 12500; break;
    case HomeCommand::tune_up:
    case HomeCommand::step_up: g_state.frequency_hz += 12500; break;
    case HomeCommand::mode: g_state.mode = "IQ SIM"; break;
    case HomeCommand::filter: g_state.mode = "FILTER SIM"; break;
    case HomeCommand::gain: g_state.mode = "GAIN SIM"; break;
    case HomeCommand::frequency_entry: g_state.mode = "TUNE SIM"; break;
    default: break;
  }
  ESP_LOGI(kTag, "touch_command=%u total=%lu span_hz=%lu",
           static_cast<unsigned>(command),
           static_cast<unsigned long>(g_touch_commands),
           static_cast<unsigned long>(g_span));
}
void synth_fft(std::array<float,kBins>& fft,uint64_t frame) {
  const float phase=static_cast<float>(frame)*.055f;
  const float center=126.f+14.f*std::sin(phase*.27f);
  for (unsigned i=0;i<fft.size();++i) {
    const float x=static_cast<float>(i);
    const float station=48.f*std::exp(-std::pow((x-center)/3.f,2));
    const float neighbor=27.f*std::exp(-std::pow((x-54.f)/4.5f,2));
    const float noise=3.2f*std::sin(x*1.337f+phase)+1.8f*std::sin(x*2.07f);
    fft[i]=-106.f+station+neighbor+noise;
  }
}
}
extern "C" void app_main(void) {
  ESP_LOGI(kTag, "boot SYNTHETIC RF; P4 Waveshare 720x720 OrcUI LVGL9 proof");
  ESP_ERROR_CHECK(bsp_display_start() ? ESP_OK : ESP_FAIL);
  ESP_ERROR_CHECK(bsp_display_backlight_on());
  ESP_ERROR_CHECK(bsp_display_brightness_set(60));
  g_state.frequency_hz=152007000;
  g_state.step_hz=12500;
  g_state.span_hz=g_span;
  g_state.mode="IQ SIM";
  g_state.radio_ready=false;
  g_state.wifi_ready=false;
  g_state.battery_percent=-1;
  if (!bsp_display_lock(1000)) {
    ESP_LOGE(kTag, "display lock failed");
    return;
  }
  HomeOptions options{};options.capabilities={true,true,true,false};
  const bool created=g_home.create(lv_screen_active(),
      Display{lv_display_get_horizontal_resolution(lv_display_get_default()),
              lv_display_get_vertical_resolution(lv_display_get_default()),Shape::rectangle},on_command,nullptr,options);
  bsp_display_unlock();
  if (!created) {
    ESP_LOGE(kTag, "OrcUI create failed; inspect display and PSRAM");
    return;
  }
  std::array<float,kBins> fft{};
  uint64_t frame=0;
  int64_t previous_report=esp_timer_get_time();
  uint32_t updates=0;
  while (true) {
    synth_fft(fft,frame++);
    if (bsp_display_lock(100)) {
      g_state.span_hz=g_span;
      g_home.update(g_state);
      g_home.spectrum(fft.data(),fft.size(),-120.f,-35.f);
      g_home.waterfall_line(fft.data(),fft.size(),-120.f,-35.f);
      bsp_display_unlock();
      ++updates;
    }
    const int64_t now=esp_timer_get_time();
    if (now-previous_report>=5000000) {
      ESP_LOGI(kTag,"ui_updates_5s=%lu internal_heap=%lu psram_heap=%lu touch=%lu",
        static_cast<unsigned long>(updates),
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        static_cast<unsigned long>(g_touch_commands));
      previous_report=now;
      updates=0;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
