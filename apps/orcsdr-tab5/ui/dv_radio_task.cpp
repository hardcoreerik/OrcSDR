// ESP-IDF platform side of dv_radio: the decode worker is one FreeRTOS task with a PSRAM stack, pinned to core 0 (the DSP
// task owns core 1) at a priority below the DSP task. Same create/park/delete pattern as the other PSRAM workers: the task
// parks instead of deleting itself, and join_worker() deletes it by handle.
#if defined(ESP_PLATFORM)
#include <atomic>

#include "dv_radio.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace orcsdr::dv_radio {
namespace {
TaskHandle_t g_task = nullptr;
std::atomic<bool> g_done{false};
void (*g_fn)(void*) = nullptr;
void* g_arg = nullptr;

void trampoline(void*) {
  g_fn(g_arg);
  g_done.store(true, std::memory_order_release);
  vTaskSuspend(nullptr);
}
bool spawn(void (*fn)(void*), void* arg) {
  if (g_task != nullptr) return false;
  g_fn = fn;
  g_arg = arg;
  g_done.store(false, std::memory_order_release);
  return xTaskCreatePinnedToCoreWithCaps(trampoline, "dv_rx", 12288, nullptr, 2, &g_task, 0,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}
void join() {
  if (g_task == nullptr) return;
  for (int i = 0; i < 1000 && !g_done.load(std::memory_order_acquire); ++i) vTaskDelay(pdMS_TO_TICKS(1));
  vTaskDeleteWithCaps(g_task);
  g_task = nullptr;
}
void sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
}  // namespace

void fill_platform_hooks(Hooks& hooks) {
  hooks.spawn_worker = spawn;
  hooks.join_worker = join;
  hooks.sleep_ms = sleep_ms;
}
}  // namespace orcsdr::dv_radio
#endif
