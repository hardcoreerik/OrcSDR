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
    uint16_t initial_device_errors;
    bool clock_checked,clock_ready; /* completed check, not live RF readiness */
} orcsdr_sx126x_probe_result_t;
/* Non-UI task only. Resets the documented SX1262, checks standby/status and
 * packet-type readback, then frees its exclusively acquired SPI bus.
 * Never starts RF, changes the antenna path, or establishes RX/TX readiness. */
esp_err_t orcsdr_sx126x_probe(orcsdr_sx126x_probe_result_t* result);
/* Uses only the profile's established TCXO supply/timing, then calibrates and
 * verifies XOSC standby/error status. Restores RC standby; never starts RF. */
esp_err_t orcsdr_sx126x_clock_check(orcsdr_sx126x_probe_result_t* result);
#ifdef __cplusplus
}
#endif
