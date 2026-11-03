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
typedef enum {ORCSDR_SX126X_RX_READY,ORCSDR_SX126X_RX_PACKET,ORCSDR_SX126X_RX_REJECTED} orcsdr_sx126x_rx_event_kind_t;
typedef struct {
    orcsdr_sx126x_rx_event_kind_t kind;
    uint16_t irq;
    uint8_t length,data[255];
    int16_t rssi_dbm,snr_db;
    bool dio1_high;
} orcsdr_sx126x_rx_event_t;
typedef struct {
    uint32_t frequency_hz;
    bool external_antenna;
    bool (*keep_running)(void* context);
    void (*event)(void* context,const orcsdr_sx126x_rx_event_t* event);
    void* context;
} orcsdr_sx126x_rx_request_t;
/* Blocking worker operation, cancellable between bounded hardware operations.
 * LongFast: SF11/BW250/CR4:5, explicit header/CRC, 16-symbol preamble, sync 0x2B.
 * Only CRC-accepted payloads emit RX_PACKET. Callbacks must not draw or block.
 * Radio and bus are released on cancellation or error. Never transmits. */
esp_err_t orcsdr_sx126x_receive_longfast(const orcsdr_sx126x_rx_request_t* request,
                                      orcsdr_sx126x_probe_result_t* result);
#ifdef __cplusplus
}
#endif
