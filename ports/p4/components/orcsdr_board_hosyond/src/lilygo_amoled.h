#pragma once
/* LilyGO T-Display-P4 AMOLED (RM69A10, 2-lane DSI) display backend. Internal to the board component:
 * hosyond_board_start() selects it by board profile. */
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "hosyond_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Power the panel (expander), run the init sequence, start DSI video. `bus` is the panel I2C bus. */
esp_err_t lilygo_amoled_start(i2c_master_bus_handle_t bus, hosyond_display_t *out);

/* DCS brightness 0-255. */
esp_err_t lilygo_amoled_set_brightness(uint8_t level);

#ifdef __cplusplus
}
#endif
