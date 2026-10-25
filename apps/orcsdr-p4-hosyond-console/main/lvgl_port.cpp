#include "lvgl_port.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ft5x06_touch.h"

static const char* TAG = "LVGL_PORT";
static hosyond_display_t g_panel;

static uint32_t tick_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// DIRECT render mode: LVGL draws straight into one of the panel's frame buffers and only
// the dirty rows are written back. When the last dirty area of a frame has been
// flushed the finished buffer becomes the scan-out buffer, so the panel never shows a
// half-drawn frame.
static void flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
  const int index = (px_map == g_panel.fb[0]) ? 0 : 1;
  hosyond_board_sync(&g_panel, index, area->y1, area->y2 + 1);
  if (lv_display_flush_is_last(display)) hosyond_board_flip(&g_panel, index);
  lv_display_flush_ready(display);
}

static void touch_cb(lv_indev_t*, lv_indev_data_t* data) {
  ft5x06_point_t pt;
  const bool down = ft5x06_get(&pt);
  data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->point.x = pt.x;
  data->point.y = pt.y;
}

static void lvgl_task(void*) {
  for (;;) {
    lv_lock();
    const uint32_t wait = lv_timer_handler();
    lv_unlock();
    vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 5 : (wait > 20 ? 20 : wait)));
  }
}

bool orcsdr_lvgl_lock(uint32_t timeout_ms) {
  (void)timeout_ms;  // lv_lock() blocks; the screenshot caller tolerates a short stall
  lv_lock();
  return true;
}
void orcsdr_lvgl_unlock(void) { lv_unlock(); }

lv_display_t* orcsdr_lvgl_start(const hosyond_display_t* panel) {
  g_panel = *panel;
  lv_init();
  lv_tick_set_cb(tick_ms);

  const uint32_t stride = lv_draw_buf_width_to_stride(g_panel.width, LV_COLOR_FORMAT_RGB888);
  if (stride != g_panel.stride_bytes || g_panel.fb_count < 1) {
    ESP_LOGE(TAG, "LVGL stride %u != panel stride %u: direct mode impossible", unsigned(stride),
             unsigned(g_panel.stride_bytes));
    return nullptr;
  }
  lv_display_t* display = lv_display_create(g_panel.width, g_panel.height);
  if (!display) return nullptr;
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);  // B,G,R in memory = panel order
  lv_display_set_flush_cb(display, flush_cb);
  lv_display_set_buffers(display, g_panel.fb[0], g_panel.fb_count > 1 ? g_panel.fb[1] : nullptr,
                         static_cast<uint32_t>(g_panel.fb_bytes), LV_DISPLAY_RENDER_MODE_DIRECT);

  lv_indev_t* touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touch_cb);
  lv_indev_set_display(touch, display);

  if (xTaskCreatePinnedToCore(lvgl_task, "lvgl", 12288, nullptr, 4, nullptr, 0) != pdPASS) return nullptr;
  ESP_LOGI(TAG, "LVGL %d.%d.%d on %dx%d, %d frame buffer(s), RGB888 direct mode", LVGL_VERSION_MAJOR,
           LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, g_panel.width, g_panel.height, g_panel.fb_count);
  return display;
}
