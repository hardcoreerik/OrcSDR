// OrcSDR on the Waveshare ESP32-P4-Module-DEV-KIT with the Hosyond 7in 800x480 touchscreen.
//
// The screens are the Tab5's own UI source (apps/orcsdr-tab5/ui), drawn through a thin layer that
// scales its 1280x720 coordinates onto this panel and renders with M5GFX's engine:
//   board adapter (components/orcsdr_board_hosyond)   panel power, DSI, touch
//   M5GFX scaling layer (components/orcsdr_gfx_compat) M5.Display / M5Canvas stand-ins
//   present.cpp                                        sprite -> panel frame buffer
//   home_app.cpp                                       Snapshot, touch, actions, spectrum feed
//   radio_service.cpp                                  esp-rtl-sdr receiver task
#include <cstring>
#include "M5Unified.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "audio_service.hpp"
#include "wifi_app.hpp"
#include "home_app.hpp"
#include "home_v3_ui.hpp"
#include "hosyond_board.h"
#include "present.hpp"
#include "radio_service.hpp"
#include "serial_cli.hpp"

static constexpr char TAG[] = "CONSOLE";

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "OrcSDR on Waveshare P4 + Hosyond 7in 800x480");
  hosyond_display_t panel;
  if (hosyond_board_start(&panel) != ESP_OK) {
    ESP_LOGE(TAG, "display bring-up FAILED");
    return;
  }
  if (!M5.Display.begin(panel.width, panel.height) || !present_init(&M5.Display, &panel)) {
    ESP_LOGE(TAG, "scaled display FAILED");
    return;
  }
  if (!serial_cli_start(&M5.Display)) ESP_LOGE(TAG, "serial command interface failed");
  if (hosyond_board_touch_start() != ESP_OK) ESP_LOGE(TAG, "touch start failed");

  // The receiver task still takes the 4B-era command queue; tuning now goes through radio::tune().
  QueueHandle_t commands = xQueueCreate(4, sizeof(orcsdr::waveshare4b::home_v3::Command));
  if (!commands || !orcsdr::radio::start(commands)) ESP_LOGE(TAG, "receiver task failed");

  wifi_app::start();       // C6 link comes up in the background a few seconds later
  audio_service::start();  // optional: the receiver and UI run without sound if the codec fails
  if (!home_app::start(&M5.Display)) ESP_LOGE(TAG, "home UI task failed");
  ESP_LOGI(TAG, "boot complete");
}
