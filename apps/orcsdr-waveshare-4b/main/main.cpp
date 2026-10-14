// Waveshare 4B BSP bring-up probe only: no SDR USB ownership, no C6 writes.
// Do not copy Tab5 pin mappings. Builds with native ESP-IDF 5.5.4.
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_rtl_sdr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "lvgl.h"

static constexpr const char *TAG = "orcsdr_4b_probe";
static lv_obj_t *s_touch_label = nullptr;

static void on_touch(lv_event_t *event) {
    lv_indev_t *input = lv_indev_get_act();
    if (!input || !s_touch_label) return;
    lv_point_t p{};
    lv_indev_get_point(input, &p);
    lv_label_set_text_fmt(s_touch_label, "Touch: %ld, %ld",
                          (long)p.x, (long)p.y);
    (void)event;
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Waveshare 4B developer probe: NOT a full OrcSDR firmware");
    ESP_LOGI(TAG, "esp_rtl_sdr linked version: %s", esp_rtl_sdr_get_version_string());
    ESP_LOGI(TAG, "PSRAM free: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "BSP display startup failed");
        return;
    }
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    if (!bsp_display_lock(3000)) {
        ESP_LOGE(TAG, "Could not acquire LVGL display lock");
        return;
    }
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "OrcSDR / Waveshare 4B");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_t *detail = lv_label_create(screen);
    lv_label_set_text(detail, "720x720 board probe\nNo SDR stream yet");
    lv_obj_set_style_text_color(detail, lv_color_hex(0xA0C4D0), 0);
    lv_obj_align(detail, LV_ALIGN_CENTER, 0, -20);
    s_touch_label = lv_label_create(screen);
    lv_label_set_text(s_touch_label, "Touch the screen");
    lv_obj_set_style_text_color(s_touch_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_touch_label, LV_ALIGN_CENTER, 0, 90);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, on_touch, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(screen, on_touch, LV_EVENT_PRESSING, nullptr);
    bsp_display_unlock();
    ESP_LOGI(TAG, "BOARD_PROBE display=initialized touch=awaiting-interaction radio=not-started c6=untouched");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
