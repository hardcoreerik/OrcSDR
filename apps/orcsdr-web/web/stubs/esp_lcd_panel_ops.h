// Web build of the one panel call the RF visualizer makes: draw a frame buffer to the panel. Here "the panel" is the page's 1280x720
// image, and the call converts the frame (little-endian RGB565, as the Tab5's DSI panel holds it) into it. Defined in vizctl_controller.cpp.
#pragma once
#include "esp_err.h"
typedef void* esp_lcd_panel_handle_t;
extern "C" esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end, const void* data);
