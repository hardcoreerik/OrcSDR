#include "bq27220_gauge.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define BQ27220_I2C_TIMEOUT_MS 100
#define REG_VOLTAGE 0x08
#define REG_BATTERY_STATUS 0x0A
#define REG_CURRENT 0x0C
#define REG_STATE_OF_CHARGE 0x2C

/* BatteryStatus bit positions in the little-endian 16-bit command result. */
#define STATUS_DSG (1u << 0)
#define STATUS_BATTPRES (1u << 3)
#define STATUS_CHGINH (1u << 8)
#define STATUS_FC (1u << 9)
#define STATUS_FD (1u << 15)

struct bq27220_gauge {
    i2c_master_dev_handle_t device;
};

static esp_err_t read_word(i2c_master_dev_handle_t device, uint8_t command, uint16_t *out)
{
    uint8_t bytes[2] = {0, 0};
    esp_err_t err = i2c_master_transmit_receive(device, &command, sizeof(command),
                                                bytes, sizeof(bytes), BQ27220_I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        *out = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    }
    return err;
}

esp_err_t bq27220_gauge_create(i2c_master_bus_handle_t bus, uint8_t address_7bit,
                               bq27220_gauge_t **out_gauge)
{
    if (!out_gauge) return ESP_ERR_INVALID_ARG;
    *out_gauge = NULL;
    if (!bus || address_7bit == 0 || address_7bit > 0x7F) return ESP_ERR_INVALID_ARG;

    bq27220_gauge_t *gauge = calloc(1, sizeof(*gauge));
    if (!gauge) return ESP_ERR_NO_MEM;

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address_7bit,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &config, &gauge->device);
    if (err != ESP_OK) {
        free(gauge);
        return err;
    }
    *out_gauge = gauge;
    return ESP_OK;
}

void bq27220_gauge_destroy(bq27220_gauge_t *gauge)
{
    if (!gauge) return;
    if (gauge->device) i2c_master_bus_rm_device(gauge->device);
    free(gauge);
}

esp_err_t bq27220_gauge_read(bq27220_gauge_t *gauge, bq27220_gauge_sample_t *out_sample)
{
    if (!out_sample) return ESP_ERR_INVALID_ARG;
    memset(out_sample, 0, sizeof(*out_sample));
    if (!gauge || !gauge->device) return ESP_ERR_INVALID_STATE;

    uint16_t status = 0, voltage = 0, soc = 0, current = 0;
    esp_err_t err = read_word(gauge->device, REG_BATTERY_STATUS, &status);
    if (err == ESP_OK) err = read_word(gauge->device, REG_VOLTAGE, &voltage);
    if (err == ESP_OK) err = read_word(gauge->device, REG_STATE_OF_CHARGE, &soc);
    if (err == ESP_OK && soc > 100) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) err = read_word(gauge->device, REG_CURRENT, &current);
    if (err != ESP_OK) return err;
    const int32_t signed_current = current <= INT16_MAX ? (int32_t)current : (int32_t)current - 65536;

    *out_sample = (bq27220_gauge_sample_t){
        .valid = true,
        .battery_status = status,
        .voltage_mv = voltage,
        .current_ma = (int16_t)signed_current,
        .state_of_charge_percent = (uint8_t)soc,
        .discharging = (status & STATUS_DSG) != 0,
        .charge_inhibited = (status & STATUS_CHGINH) != 0,
        .full_charge = (status & STATUS_FC) != 0,
        .full_discharge = (status & STATUS_FD) != 0,
        .battery_present_reported = (status & STATUS_BATTPRES) != 0,
    };
    return ESP_OK;
}
