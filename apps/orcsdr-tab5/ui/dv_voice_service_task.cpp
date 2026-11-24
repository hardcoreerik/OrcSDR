// ESP-IDF platform side of dv_voice_service: one pump task with a PSRAM stack on core 1 (the vocoder is serialized with the
// P25 voice task by priority, never by sharing state), below the DSP task.
#if defined(ESP_PLATFORM)
#include <atomic>

#include "dv_voice_service.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace orcsdr::dv_voice_service {
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
  return xTaskCreatePinnedToCoreWithCaps(trampoline, "dv_voice", 12288, nullptr, 4, &g_task, 1,
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
  hooks.spawn = spawn;
  hooks.join = join;
  hooks.sleep_ms = sleep_ms;
}
}  // namespace orcsdr::dv_voice_service
#endif
