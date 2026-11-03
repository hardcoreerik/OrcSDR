#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bq27220_gauge bq27220_gauge_t;

/* Read-only standard-command snapshot. BATTPRES is reported raw by the gauge;
   it is not treated as proof of a fitted cell because its behavior depends on
   the gauge's BIEnable configuration. A snapshot is valid only when every
   command read succeeds and SOC is within the documented 0..100 range. */
typedef struct {
    bool valid;
    uint16_t battery_status;
    uint16_t voltage_mv;
    int16_t current_ma;
    uint8_t state_of_charge_percent;
    bool discharging;
    bool charge_inhibited;
    bool full_charge;
    bool full_discharge;
    bool battery_present_reported;
} bq27220_gauge_sample_t;

/* The caller owns the existing bus. This creates only an address-specific
   device handle; it does not configure, probe by writing, or own the bus. */
esp_err_t bq27220_gauge_create(i2c_master_bus_handle_t bus, uint8_t address_7bit,
                               bq27220_gauge_t **out_gauge);
void bq27220_gauge_destroy(bq27220_gauge_t *gauge);

/* Reads standard commands only: BatteryStatus, Voltage, StateOfCharge,
   Current. On any error the output is cleared and remains invalid. */
esp_err_t bq27220_gauge_read(bq27220_gauge_t *gauge,
                             bq27220_gauge_sample_t *out_sample);

#ifdef __cplusplus
}
#endif
