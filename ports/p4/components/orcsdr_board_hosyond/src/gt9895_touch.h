#pragma once
#include "hosyond_board.h"
esp_err_t gt9895_touch_start(i2c_master_bus_handle_t bus);
esp_err_t gt9895_touch_poll(orcsdr_touch_sample_t *out);
