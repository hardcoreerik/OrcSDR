/* SPDX-License-Identifier: MIT
 *
 * GT9895 touch polling for the LilyGO T-Display-P4 AMOLED.
 *
 * Protocol knowledge comes from SensorLib's MIT-licensed TouchDrvGT9895 (Lewis He); see
 * gt9895_protocol.h and docs/hardware/P4_REUSE.md. Pins and the I2C address come from the
 * board profile. This file is OrcSDR's own code.
 *
 * One finger only: multi-contact reports are ignored rather than guessed at, so a second
 * finger never moves the pointer. Raw 1060x2400 coordinates are scaled linearly to the
 * 568x1232 native panel; corner/centre accuracy is a physical acceptance item.
 */
#include "gt9895_touch.h"
#include "gt9895_protocol.h"
#include "orcsdr_board_profile.h"
#include "lilygo_amoled.h"
#include "esp_log.h"

#define GT9895_I2C_ADDR 0x5D
#define GT9895_I2C_HZ 400000
#define GT9895_I2C_TIMEOUT_MS 20
#define GT9895_RAW_W 1060
#define GT9895_RAW_H 2400
#define GT9895_NATIVE_W 568
#define GT9895_NATIVE_H 1232

static const char *TAG = "GT9895";
static i2c_master_dev_handle_t s_dev;
static orcsdr_point_t s_last;

static esp_err_t read_reg(uint32_t address, uint8_t *out, size_t n)
{
    uint8_t a[4];
    gt9895_address_bytes(address, a);
    return i2c_master_transmit_receive(s_dev, a, sizeof(a), out, n, GT9895_I2C_TIMEOUT_MS);
}

/* Acknowledge the report so the controller can post the next one. */
static esp_err_t clear_event(void)
{
    uint8_t cmd[5];
    gt9895_address_bytes(GT9895_ADDR_POINT, cmd);
    cmd[4] = 0;
    return i2c_master_transmit(s_dev, cmd, sizeof(cmd), GT9895_I2C_TIMEOUT_MS);
}

esp_err_t gt9895_touch_start(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (s_dev) return ESP_OK;

    esp_err_t e = lilygo_amoled_touch_reset(bus);
    if (e != ESP_OK) ESP_LOGW(TAG, "touch reset pulse failed (%s); trying to talk to the controller anyway", esp_err_to_name(e));

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GT9895_I2C_ADDR,
        .scl_speed_hz = GT9895_I2C_HZ,
    };
    i2c_master_dev_handle_t dev;
    e = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (e != ESP_OK) return e;
    s_dev = dev;

    uint8_t fw[GT9895_FW_BLOCK_LEN];
    e = ESP_FAIL;
    for (int attempt = 0; attempt < 3 && e != ESP_OK; attempt++) {
        e = read_reg(GT9895_ADDR_FW_VERSION, fw, sizeof(fw));
        if (e == ESP_OK && !gt9895_firmware_ok(fw, sizeof(fw))) e = ESP_ERR_INVALID_RESPONSE;
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "controller not identified as GT9895 at 0x%02X (%s)", GT9895_I2C_ADDR, esp_err_to_name(e));
        i2c_master_bus_rm_device(dev);
        s_dev = NULL;
        return e;
    }
    ESP_LOGI(TAG, "GT9895 identified; raw %dx%d scaled to %dx%d; single contact", GT9895_RAW_W, GT9895_RAW_H,
             GT9895_NATIVE_W, GT9895_NATIVE_H);
    return ESP_OK;
}

esp_err_t gt9895_touch_poll(orcsdr_touch_sample_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t head[GT9895_EVENT_HEAD_LEN];
    esp_err_t e = read_reg(GT9895_ADDR_POINT, head, sizeof(head));
    if (e != ESP_OK) return e;

    unsigned contacts = 0;
    switch (gt9895_parse_header(head, sizeof(head), &contacts)) {
    case GT9895_EVENT_NONE:
        return ESP_ERR_NOT_FOUND;
    case GT9895_EVENT_INVALID:
        clear_event();
        return ESP_ERR_INVALID_CRC;
    case GT9895_EVENT_IGNORE:
        clear_event();
        return ESP_ERR_NOT_FOUND;
    case GT9895_EVENT_RELEASE:
        clear_event();
        *out = (orcsdr_touch_sample_t){.position = s_last, .pressed = false};
        return ESP_OK;
    case GT9895_EVENT_CONTACTS:
        break;
    }

    if (contacts != 1) { /* unsupported: keep the previous state and acknowledge */
        clear_event();
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t block[GT9895_BYTES_PER_POINT + GT9895_SUM_LEN];
    e = read_reg(GT9895_ADDR_POINT + GT9895_EVENT_HEAD_LEN, block, sizeof(block));
    clear_event();
    if (e != ESP_OK) {
        return e;
    }
    int32_t rx, ry;
    if (!gt9895_parse_first_contact(block, sizeof(block), contacts, &rx, &ry)) {
        return ESP_ERR_INVALID_CRC;
    }
    s_last = (orcsdr_point_t){.x = gt9895_scale(rx, GT9895_RAW_W, GT9895_NATIVE_W),
                              .y = gt9895_scale(ry, GT9895_RAW_H, GT9895_NATIVE_H)};
    *out = (orcsdr_touch_sample_t){.position = s_last, .pressed = true};
    return ESP_OK;
}
