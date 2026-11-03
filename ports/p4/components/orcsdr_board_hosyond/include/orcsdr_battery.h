#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bq27220_gauge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the process-lifetime polling worker on an existing I2C master bus.
   The bus must remain valid for the lifetime of the process. There is
   intentionally no stop API: removing the device while the worker polls is
   unsafe. Duplicate starts return ESP_ERR_INVALID_STATE. */
esp_err_t orcsdr_battery_start(i2c_master_bus_handle_t bus, uint8_t address_7bit);

typedef struct {
    bq27220_gauge_sample_t gauge; /* cleared after any failed read */
    esp_err_t last_error;
    bool gauge_seen;             /* at least one complete read succeeded */
    bool valid;                   /* fresh, reported-present, nonzero voltage */
    bool stale;                   /* no successful sample, or age >= 3000 ms */
    uint32_t last_success_age_ms; /* UINT32_MAX until first successful read */
    int32_t ui_percent;           /* -1 when unavailable; otherwise 0..100 */
} orcsdr_battery_snapshot_t;

/* Copies the latest state without waiting for I2C. Returns
   ESP_ERR_INVALID_STATE until start succeeds. */
esp_err_t orcsdr_battery_get_snapshot(orcsdr_battery_snapshot_t *out_snapshot);

#ifdef ORCSDR_BATTERY_TESTING
/* Calls the same polling operation as the worker, for deterministic host tests. */
void orcsdr_battery_test_poll_once(void);
#endif

#ifdef __cplusplus
}
#endif
