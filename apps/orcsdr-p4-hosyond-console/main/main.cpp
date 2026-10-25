// OrcSDR console on the Waveshare ESP32-P4-Module-DEV-KIT + Hosyond 7in 800x480 touchscreen.
// Layers: board adapter (components/orcsdr_board_hosyond) -> LVGL port (lvgl_port.*) ->
// adaptive Home view (experiments/orcui, geometry from orcui::plan(Display)) fed by the
// receiver service (radio_service.*) through immutable snapshots.
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hosyond_board.h"
#include "lvgl_port.hpp"
#include "orcui_4b_adapter.hpp"
#include "radio_service.hpp"
#include "screen_capture.hpp"

using namespace orcsdr;
static constexpr char TAG[] = "CONSOLE";

// LVGL task only: pull a snapshot, never block the radio.
static void ui_timer(lv_timer_t*) {
  uint8_t fft[256]{};
  const bool fresh = radio::take_bins(fft);
  waveshare4b::adaptive::update(radio::snapshot(), fft, fresh);
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "OrcSDR console, Waveshare P4 + Hosyond 7in 800x480");
  hosyond_display_t panel;
  if (hosyond_board_start(&panel) != ESP_OK) {
    ESP_LOGE(TAG, "display bring-up FAILED");
    return;
  }
  QueueHandle_t commands = xQueueCreate(12, sizeof(waveshare4b::home_v3::Command));
  if (!commands) {
    ESP_LOGE(TAG, "command queue failed");
    return;
  }
  lv_display_t* display = orcsdr_lvgl_start(&panel);
  if (!display) {
    ESP_LOGE(TAG, "LVGL start FAILED");
    return;
  }
  orcsdr_lvgl_lock(1000);
  const bool home = waveshare4b::adaptive::create(commands);
  if (home) lv_timer_create(ui_timer, 150, nullptr);
  orcsdr_lvgl_unlock();
  if (!home) {
    ESP_LOGE(TAG, "adaptive Home creation FAILED");
    return;
  }
  if (!waveshare4b::screen_capture::start(waveshare4b::adaptive::prepare_capture))
    ESP_LOGE(TAG, "screenshot service failed");
  if (hosyond_board_touch_start() != ESP_OK) ESP_LOGE(TAG, "touch start failed");
  if (!radio::start(commands)) ESP_LOGE(TAG, "receiver task failed");
  ESP_LOGI(TAG, "boot complete");
}
