#include "orcsdr_restart.hpp"

#include <cstdint>
#include <cstdio>

#include <esp_attr.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/wdt_hal.h>
#include <soc/rtc.h>

namespace {
constexpr uint32_t kCleanRestartMagic = 0x4f52434cu;  // "ORCL"
RTC_NOINIT_ATTR uint32_t g_clean_restart_magic;
RTC_NOINIT_ATTR uint32_t g_original_reason;
}  // namespace

[[noreturn]] void orcsdr_restart_clean() {
  fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(25));  // let queued serial output drain
  portDISABLE_INTERRUPTS();
  wdt_hal_context_t rwdt{};
  wdt_hal_init(&rwdt, WDT_RWDT, 0, false);
  const uint32_t ticks = static_cast<uint32_t>(50ULL * rtc_clk_slow_freq_get_hz() / 1000ULL);
  wdt_hal_write_protect_disable(&rwdt);
  wdt_hal_config_stage(&rwdt, WDT_STAGE0, ticks, WDT_STAGE_ACTION_RESET_SYSTEM);
  wdt_hal_config_stage(&rwdt, WDT_STAGE1, ticks, WDT_STAGE_ACTION_RESET_RTC);
  wdt_hal_enable(&rwdt);
  wdt_hal_write_protect_enable(&rwdt);
  for (;;) {
  }
}

void orcsdr_normalize_reset_at_boot() {
  const esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_POWERON) g_clean_restart_magic = 0;  // RTC memory is undefined after power-on
  if (g_clean_restart_magic == kCleanRestartMagic) {
    g_clean_restart_magic = 0;  // we are the second boot of a clean restart: carry on
    printf("RTL_RESET_NORMALIZED original_reason=%u\n", static_cast<unsigned>(g_original_reason));
    return;
  }
  const bool cpu_level = reason == ESP_RST_SW || reason == ESP_RST_PANIC ||
                         reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT;
  if (!cpu_level) return;
  g_original_reason = static_cast<uint32_t>(reason);
  g_clean_restart_magic = kCleanRestartMagic;
  printf("RTL_RESET_NORMALIZE original_reason=%d restarting_cleanly\n", static_cast<int>(reason));
  orcsdr_restart_clean();
}
