#pragma once
/*
 * Display and touch hardware adapter: Waveshare ESP32-P4-Module-DEV-KIT with the
 * Hosyond 7" 800x480 MIPI-DSI touchscreen.
 *
 * Owns panel power, the DSI link, the TC358762 bridge, the DPI frame buffers, the
 * backlight and the FT5x06 touch controller. Shared application and UI code must
 * only use this interface, never panel registers, pins or reset sequencing.
 *
 * Pixel format: 24 bpp, bytes in memory are [B][G][R] (what this panel needs, and
 * the in-memory layout of LVGL's LV_COLOR_FORMAT_RGB888). Frame buffers live in
 * PSRAM, are cache-line aligned by the IDF, and are scanned out continuously.
 *
 * Threading: hosyond_board_start() runs once from a normal task. hosyond_board_flip()
 * may be called from the UI task only.
 */
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "ft5x06_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOSYOND_BOARD_MAX_FBS 2

typedef struct {
    int width;                  /* 800 */
    int height;                 /* 480 */
    int bytes_per_pixel;        /* 3 */
    size_t stride_bytes;        /* width * bytes_per_pixel; frame buffers are packed */
    size_t fb_bytes;            /* stride_bytes * height */
    int fb_count;               /* 1 or 2 */
    uint8_t *fb[HOSYOND_BOARD_MAX_FBS];
} hosyond_display_t;

/* Power the panel, bring up DSI and the bridge, release touch reset, backlight on. */
esp_err_t hosyond_board_start(hosyond_display_t *out);

/* Write back the cache for rows [y0, y1) of the given frame buffer (UI flush path). */
esp_err_t hosyond_board_sync(const hosyond_display_t *d, int fb_index, int y0, int y1);

/* Make `fb_index` the buffer being scanned out. Rows must already be synced. */
esp_err_t hosyond_board_flip(const hosyond_display_t *d, int fb_index);

/* Start the polled touch task (see ft5x06_touch.h for ft5x06_get()). */
esp_err_t hosyond_board_touch_start(void);

#ifdef __cplusplus
}
#endif
