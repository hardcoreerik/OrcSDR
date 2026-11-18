#include "iq_tap.hpp"
#include <atomic>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "radio_service.hpp"

namespace iq_tap {
namespace {
constexpr char TAG[] = "IQ_TAP";
constexpr size_t kBlock = 16384;

StreamBufferHandle_t g_stream = nullptr;
TaskHandle_t g_task = nullptr;
Consumer g_consumer = nullptr;
void* g_context = nullptr;
std::atomic<bool> g_run{false};
std::atomic<bool> g_exited{true};
bool g_psram_stack = false;
std::atomic<uint32_t> g_dropped{0}, g_blocks{0};
std::atomic<int> g_in_sink{0};   // the radio task is inside sink(): the stream buffer must not be freed under it

void sink(const uint8_t* iq, size_t bytes) {
  g_in_sink.fetch_add(1);
  StreamBufferHandle_t s = g_stream;
  if (s && g_run.load()) {
    if (xStreamBufferSpacesAvailable(s) < bytes || xStreamBufferSend(s, iq, bytes, 0) != bytes) g_dropped.fetch_add(1);
  }
  g_in_sink.fetch_sub(1);
}

void task(void*) {
  auto* buf = static_cast<uint8_t*>(heap_caps_malloc(kBlock, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  while (g_run.load() && buf) {
    const size_t got = xStreamBufferReceive(g_stream, buf, kBlock, pdMS_TO_TICKS(50));
    if (got < 2) continue;
    g_blocks.fetch_add(1);
    g_consumer(buf, got & ~static_cast<size_t>(1), g_context);
  }
  heap_caps_free(buf);
  g_exited = true;
  // A task with a PSRAM stack must not delete itself on this IDF (it would need a temporary task from internal RAM): it parks and stop() deletes it.
  if (g_psram_stack) vTaskSuspend(nullptr);
  vTaskDelete(nullptr);
}
}  // namespace

bool start(const char* name, Consumer consumer, void* context, size_t buffer_bytes, int core, int priority, size_t stack_bytes, bool stack_in_psram) {
  if (g_run.load() || !g_exited.load() || !consumer) return false;
  g_stream = xStreamBufferCreateWithCaps(buffer_bytes, 1, MALLOC_CAP_SPIRAM);
  if (!g_stream) {
    ESP_LOGE(TAG, "buffer allocation failed");
    return false;
  }
  g_consumer = consumer;
  g_context = context;
  g_dropped = 0;
  g_blocks = 0;
  g_run = true;
  g_exited = false;
  g_psram_stack = stack_in_psram;
  const BaseType_t created = stack_in_psram
      ? xTaskCreatePinnedToCoreWithCaps(task, name, stack_bytes, nullptr, priority, &g_task, core, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
      : xTaskCreatePinnedToCore(task, name, stack_bytes, nullptr, priority, &g_task, core);
  if (created != pdPASS) {
    g_run = false;
    g_exited = true;
    vStreamBufferDeleteWithCaps(g_stream);
    g_stream = nullptr;
    return false;
  }
  orcsdr::radio::set_aux_iq_sink(sink);
  return true;
}

void stop() {
  if (!g_run.load() && g_exited.load()) return;
  orcsdr::radio::set_aux_iq_sink(nullptr);
  g_run = false;
  // The consumer may be inside a long block; wait for it (and for the radio task to leave sink()) before the buffer is freed.
  // If it does not stop, leak the buffer rather than free memory a task is still using.
  for (int i = 0; i < 500 && (!g_exited.load() || g_in_sink.load() != 0); ++i) vTaskDelay(pdMS_TO_TICKS(10));
  if (!g_exited.load() || g_in_sink.load() != 0) {
    ESP_LOGE(TAG, "consumer did not stop in 5 s (exited=%d in_sink=%d): the stream buffer is kept", int(g_exited.load()), g_in_sink.load());
    return;
  }
  if (g_psram_stack && g_exited.load() && g_task != nullptr) {
    vTaskDeleteWithCaps(g_task);
    g_task = nullptr;
    g_psram_stack = false;
  }
  if (g_stream) {
    StreamBufferHandle_t s = g_stream;
    g_stream = nullptr;
    vStreamBufferDeleteWithCaps(s);
  }
}

bool running() { return g_run.load(); }
uint32_t dropped() { return g_dropped.load(); }
uint32_t blocks() { return g_blocks.load(); }
}  // namespace iq_tap
