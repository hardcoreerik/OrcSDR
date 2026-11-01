#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called after the profile's panel bus exists. Owns expander device lifetime. */
esp_err_t orcsdr_board_controls_start(i2c_master_bus_handle_t panel_bus);
/* Separate codec/IMU bus where the profile provides one; otherwise panel bus. */
i2c_master_bus_handle_t orcsdr_board_secondary_i2c(void);
/* Assert reset low, or release EN to the board pull-up; no Hosted API calls. */
esp_err_t orcsdr_board_c6_enable(bool enable);
#ifdef __cplusplus
}
#endif
