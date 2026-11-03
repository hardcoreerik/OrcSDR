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
/* Packet-radio lines share the expander owner/lock with C6 and board rails.
 * These calls do not select an antenna, configure RF or authorize TX. */
esp_err_t orcsdr_board_radio_reset(bool asserted);
esp_err_t orcsdr_board_radio_dio1_read(bool* high);
/* Select only a documented antenna-switch polarity; never authorizes TX. */
esp_err_t orcsdr_board_radio_select_antenna(bool external);
#ifdef __cplusplus
}
#endif
