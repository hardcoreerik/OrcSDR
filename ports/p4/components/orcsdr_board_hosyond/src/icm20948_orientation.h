#pragma once

#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LilyGO T-Display P4 orientation-sensor provider. The caller owns the
   secondary I2C bus (documented SDA20/SCL21); this provider owns only its
   address-0x68 device handle. No sensor-to-panel axis mapping is assumed. */
typedef struct { float x_g, y_g, z_g; } icm20948_orientation_sample_t;

esp_err_t icm20948_orientation_start(i2c_master_bus_handle_t secondary_bus);
esp_err_t icm20948_orientation_read(icm20948_orientation_sample_t *sample);
void icm20948_orientation_stop(void);

#ifdef __cplusplus
}
#endif
