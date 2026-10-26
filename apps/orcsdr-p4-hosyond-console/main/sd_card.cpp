#include "sd_card.hpp"
#include <atomic>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orcsdr_storage.hpp"

namespace sd_card {
namespace {
constexpr char TAG[] = "SD_CARD";
constexpr gpio_num_t kPowerEnable = GPIO_NUM_45;  // active low
std::atomic<bool> g_attempted{false};

void task(void*) {
  vTaskDelay(pdMS_TO_TICKS(2500));  // after the display, touch and receiver
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kPowerEnable;
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  gpio_set_level(kPowerEnable, 0);  // power the card
  vTaskDelay(pdMS_TO_TICKS(50));
  const bool ok = orcsdr::storage::mount_tab5_sd();
  g_attempted = true;
  if (ok) ESP_LOGI(TAG, "microSD mounted, %llu MB", (unsigned long long)(orcsdr::storage::total_bytes() >> 20));
  else ESP_LOGI(TAG, "no microSD card mounted");
  vTaskDelete(nullptr);
}
}  // namespace

void start() { xTaskCreatePinnedToCore(task, "sd_mount", 6144, nullptr, 2, nullptr, 0); }

Status status() {
  Status s;
  s.attempted = g_attempted.load();
  s.mounted = orcsdr::storage::mounted();
  s.total_bytes = orcsdr::storage::total_bytes();
  return s;
}
}  // namespace sd_card
