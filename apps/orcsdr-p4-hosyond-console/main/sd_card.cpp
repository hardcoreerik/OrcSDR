#include "sd_card.hpp"
#include <atomic>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orcsdr_board_profile.h"
#include "orcsdr_storage.hpp"

namespace sd_card {
namespace {
constexpr char TAG[] = "SD_CARD";
std::atomic<bool> g_attempted{false};
std::atomic<uint64_t> g_free_bytes{0};
std::atomic<bool> g_free_known{false};

void task(void*) {
  vTaskDelay(pdMS_TO_TICKS(2500));  // after the display, touch and receiver
  const orcsdr_board_profile_t* board = orcsdr_board_profile();
  if (!board->has_sd) {
    g_attempted = true;
    ESP_LOGI(TAG, "this board has no microSD slot");
    vTaskDelete(nullptr);
    return;
  }
  if (board->sd_power_enable >= 0) {  // a board without a power switch pin powers the slot by other means
    const gpio_num_t kPowerEnable = static_cast<gpio_num_t>(board->sd_power_enable);
    const int active = board->sd_power_active_low ? 0 : 1;
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << kPowerEnable;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(kPowerEnable, active);  // power the card
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  orcsdr::storage::SdHardware hardware;
  hardware.power_ldo_channel=-1; // selected board adapter owns the rail, not the display's LDO4
  const bool configured=orcsdr::storage::configure_sd_hardware(hardware);
  const bool ok=configured && orcsdr::storage::mount_tab5_sd();
  g_attempted = true;
  if (ok) ESP_LOGI(TAG, "microSD mounted, %llu MB", (unsigned long long)(orcsdr::storage::total_bytes() >> 20));
  else ESP_LOGI(TAG, "no microSD card mounted");
  // statvfs can touch the card; keep it on this background task, never the UI.
  while(ok) {
    uint64_t available=0;
    const bool known=orcsdr::storage::query_free_bytes(&available);
    g_free_bytes.store(available);g_free_known.store(known);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
  vTaskDelete(nullptr);
}
}  // namespace

void start() { xTaskCreatePinnedToCore(task, "sd_mount", 6144, nullptr, 2, nullptr, 0); }

Status status() {
  Status s;
  s.attempted = g_attempted.load();
  s.mounted = orcsdr::storage::mounted();
  s.total_bytes = orcsdr::storage::total_bytes();
  s.free_known = g_free_known.load();
  s.free_bytes = g_free_bytes.load();
  return s;
}
}  // namespace sd_card
