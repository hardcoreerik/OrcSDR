// The firmware side of sd_writer: one low-priority task on core 0 (away from the DSP on core 1) that sleeps until a job
// is submitted, and file access through orcsdr::storage. The task stack is in PSRAM; the task is created on the first
// job and then stays.
#include "sd_writer.hpp"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include "orcsdr_storage.hpp"

namespace orcsdr::sd_writer {
namespace {

constexpr uint32_t kPauseMs = 5;          // after each piece
constexpr UBaseType_t kTaskPriority = 1;  // same as the main loop; far below USB (20), IQ delivery (19) and DSP (6)
constexpr uint32_t kTaskStackBytes = 6144;

storage::File g_file;
TaskHandle_t g_task = nullptr;

void* open_file(const char* path) {
  g_file = storage::filesystem().open(path, FILE_WRITE, true);
  return g_file ? &g_file : nullptr;
}
size_t write_file(void* file, const uint8_t* data, size_t size) { return static_cast<storage::File*>(file)->write(data, size); }
bool close_file(void* file) { return static_cast<storage::File*>(file)->close(); }
void* alloc_staging(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED);
}
void free_staging(void* staging) { heap_caps_free(staging); }
void pause() { vTaskDelay(pdMS_TO_TICKS(kPauseMs)); }
void wake();

constexpr Platform kPlatform = [] {
  Platform p;
  p.open = open_file;
  p.write = write_file;
  p.close = close_file;
  p.alloc_staging = alloc_staging;
  p.free_staging = free_staging;
  p.pause = pause;
  p.wake = wake;
  p.chunk_bytes = 32 * 1024;
  return p;
}();

Writer g_writer(kPlatform);

void task(void*) {
  for (;;) {
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (g_writer.run_once()) {
    }
  }
}

void wake() {
  if (g_task == nullptr &&
      xTaskCreatePinnedToCoreWithCaps(task, "sd_writer", kTaskStackBytes, nullptr, kTaskPriority, &g_task, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    g_task = nullptr;
    (void)g_writer.run_once();  // no task: write inline rather than lose the job
    return;
  }
  xTaskNotifyGive(g_task);
}

}  // namespace

Writer& shared() { return g_writer; }

}  // namespace orcsdr::sd_writer
