#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool detected;
    uint8_t chip_mode,command_status;
    uint16_t device_errors;
} orcsdr_sx126x_probe_result_t;
/* Non-UI task only. Resets the documented SX1262, checks standby/status and
 * packet-type readback, then frees its exclusively acquired SPI bus.
 * Never starts RF, changes the antenna path, or establishes RX/TX readiness. */
esp_err_t orcsdr_sx126x_probe(orcsdr_sx126x_probe_result_t* result);
#ifdef __cplusplus
}
#endif
