#pragma once
/*
 * FT5x06 touch controller on the Raspberry Pi 7"-compatible panel (I2C 0x38).
 *
 * A task polls the controller every 10 ms and keeps the latest point; callers read
 * it with ft5x06_get(). Touch reset (ATTINY PORTC bit 1) must already be released.
 *
 * Register map (FT5x06 family): 0x02 TD_STATUS (touch count in [3:0]), then per
 * point from 0x03: XH (event [7:6], X[11:8]), XL, YH (id [7:4], Y[11:8]), YL.
 * This panel reports almost only event 2 (contact), so a press is "any touch after
 * none". Both axes run opposite to the display's (up moved down, left moved right
 * on 2026-10-25) and are mirrored into display coordinates here. Stationary-finger
 * dropouts of 50 ms or more occur, so a release needs 10 empty 10 ms polls.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FT5X06_I2C_ADDR 0x38
#define FT5X06_X_RES 800
#define FT5X06_Y_RES 480
#define FT5X06_RELEASE_POLLS 10

typedef struct {
    int x, y;        /* display coordinates, valid while pressed */
    bool pressed;    /* finger currently on the glass */
    uint32_t seq;    /* counts new presses/positions */
} ft5x06_point_t;

/* `tag` must outlive the task (use a string literal). */
esp_err_t ft5x06_start(i2c_master_bus_handle_t bus, const char *tag);

/* Consistent snapshot of the latest point; returns `pressed`. Safe from any task. */
bool ft5x06_get(ft5x06_point_t *out);

#ifdef __cplusplus
}
#endif
