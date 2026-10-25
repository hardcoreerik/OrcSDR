#pragma once
// Thin LVGL 9 port for the Hosyond board component: RGB888 direct-render display on the
// panel's own PSRAM frame buffers (no copy, double-buffered flip) plus a pointer input
// device fed by the FT5x06 task. Contract for backend authors: docs/orcui/.
#include <cstdint>
#include "lvgl.h"
#include "hosyond_board.h"

lv_display_t* orcsdr_lvgl_start(const hosyond_display_t* panel); // creates display, indev, UI task
bool orcsdr_lvgl_lock(uint32_t timeout_ms);                    // required around any LVGL call
void orcsdr_lvgl_unlock(void);
