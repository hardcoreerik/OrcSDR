#pragma once

/*
 * FT5x06 touch controller on the Raspberry Pi 7"-compatible panel (I2C 0x38).
 *
 * A task polls the controller every 10 ms and keeps the latest point; callers
 * read it with ft5x06_get(). The touch reset must already be released
 * (ATTINY PORTC bit 1) before ft5x06_start() is called.
 *
 * Register map (FT5x06 family): 0x02 TD_STATUS (touch count in [3:0]), then
 * 6 bytes per point from 0x03: XH (event [7:6], X[11:8]), XL, YH (touch id
 * [7:4], Y[11:8]), YL. Event: 0 down, 1 up, 2 contact, 3 none.
 *
 * Axes: on this panel both axes run opposite to the display's (observed
 * 2026-10-25: up moved down, left moved right), so the controller's native
 * 800x480 range is mirrored into display coordinates here.
 */

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

#define FT5X06_I2C_ADDR 0x38
#define FT5X06_REG_STATUS 0x02
#define FT5X06_X_RES 800
#define FT5X06_Y_RES 480
#define FT5X06_RELEASE_POLLS 10  /* consecutive empty 10 ms polls before a release; stationary-finger dropouts of 50+ ms were measured */

typedef struct {
    int x, y;            /* display coordinates, valid while pressed */
    bool pressed;        /* finger currently on the glass */
    uint32_t seq;        /* increments on every new press or position */
} ft5x06_point_t;

static i2c_master_dev_handle_t s_ft5x06_dev;
static volatile ft5x06_point_t s_ft5x06_pt;

static inline bool ft5x06_get(ft5x06_point_t *out)
{
    /* Two reads of seq around the copy give a consistent snapshot without a lock. */
    uint32_t a, b;
    do {
        a = s_ft5x06_pt.seq;
        out->x = s_ft5x06_pt.x;
        out->y = s_ft5x06_pt.y;
        out->pressed = s_ft5x06_pt.pressed;
        b = s_ft5x06_pt.seq;
    } while (a != b);
    out->seq = a;
    return out->pressed;
}

static void ft5x06_task(void *arg)
{
    const char *tag = (const char *)arg;
    int last_event = 3;
    int empty = FT5X06_RELEASE_POLLS;
    int fails = 0;
    uint32_t polls = 0, events = 0, errors = 0;
    uint8_t d[5] = {0};
    for (;;) {
        uint8_t reg = FT5X06_REG_STATUS;
        if ((++polls % 500) == 0) {
            ESP_LOGI(tag, "touch alive: polls=%u touch-samples=%u i2c-errors=%u raw=%02X %02X %02X %02X %02X",
                     (unsigned)polls, (unsigned)events, (unsigned)errors,
                     d[0], d[1], d[2], d[3], d[4]);
        }
        if (i2c_master_transmit_receive(s_ft5x06_dev, &reg, 1, d, sizeof(d), 20) != ESP_OK) {
            errors++;
            if (++fails == 100) {
                ESP_LOGW(tag, "touch: 100 consecutive I2C read failures");
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        fails = 0;
        if ((d[0] & 0x0f) > 0 && ((d[1] >> 6) & 3) != 3) {
            events++;
        }
        const int n = d[0] & 0x0f;
        const int ev = (d[1] >> 6) & 3;
        if (n > 0 && n <= 10 && ev != 3) {
            int x = (FT5X06_X_RES - 1) - (((d[1] & 0x0f) << 8) | d[2]);
            int y = (FT5X06_Y_RES - 1) - (((d[3] & 0x0f) << 8) | d[4]);
            if (x < 0) {
                x = 0;
            }
            if (y < 0) {
                y = 0;
            }
            if (ev == 1) {
                s_ft5x06_pt.pressed = false;
            } else {
                s_ft5x06_pt.x = x;
                s_ft5x06_pt.y = y;
                s_ft5x06_pt.pressed = true;
                s_ft5x06_pt.seq++;
            }
            /* This panel's firmware mostly reports "contact" (2) and rarely
             * "down" (0), so a press is any event arriving after none. */
            if (last_event == 3 && ev != 1) {
                ESP_LOGI(tag, "TOUCH press x=%d y=%d points=%d", x, y, n);
            }
            last_event = ev;
            empty = 0;
        } else if (++empty >= FT5X06_RELEASE_POLLS) {
            /* A single empty read mid-touch is common on this panel (seen as
             * press-release-press within 50 ms), so release needs several. */
            s_ft5x06_pt.pressed = false;
            if (last_event != 3) {
                ESP_LOGI(tag, "TOUCH release");
            }
            last_event = 3;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* `tag` must outlive the task (use a string literal). */
static inline esp_err_t ft5x06_start(i2c_master_bus_handle_t bus, const char *tag)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT5X06_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_ft5x06_dev);
    if (err != ESP_OK) {
        return err;
    }
    return xTaskCreate(ft5x06_task, "ft5x06", 3072, (void *)tag, 5, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
