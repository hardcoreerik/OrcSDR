#pragma once
/* Battery-backed clock service for boards whose profile declares an RTC (LilyGO T-Display-P4: PCF8563).
 *
 * The RTC always holds UTC. orcsdr_rtc_start() restores the system clock from it when the chip says its time
 * is trustworthy (a bounded wait of at most about 1.2 s for the chip's seconds to tick), and afterwards the chip
 * is written from the system clock whenever asked. The caller (the network clock) asks after every SNTP step or
 * manual set, so the chip follows the clock people can see and correct.
 *
 * There is deliberately no task: the board's internal and DMA RAM have almost no headroom, and a task stack broke
 * the C6 SDIO bring-up. A write is one-shot esp_timer callback aimed at the next whole second. */
#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool running;            /* worker started and chip answered */
    bool chip_time_valid;    /* last read decoded and VL was clear */
    bool vl_flag;            /* last read reported the voltage-low flag (battery lost or never set) */
    bool restored;           /* system clock was set from the chip at start */
    uint32_t chip_utc;       /* last time read from the chip; 0 if none */
    uint32_t last_write_utc; /* last time written and verified; 0 if none */
    uint32_t writes_ok, writes_failed;
    esp_err_t last_error;
} orcsdr_rtc_status_t;

/* Starts the worker on an existing I2C master bus (must outlive the process). Duplicate starts return
 * ESP_ERR_INVALID_STATE. ESP_ERR_NOT_FOUND when the chip does not answer. */
esp_err_t orcsdr_rtc_start(i2c_master_bus_handle_t bus, uint8_t address_7bit);

/* Asks the worker to copy the system clock into the chip on the next whole second. Returns immediately.
 * ESP_ERR_INVALID_STATE when the service is not running. */
esp_err_t orcsdr_rtc_request_write_from_system(void);

/* Copies the latest state without touching I2C. Returns ESP_ERR_INVALID_STATE (zeroed output) when not running. */
esp_err_t orcsdr_rtc_get_status(orcsdr_rtc_status_t *out);

#ifdef __cplusplus
}
#endif
