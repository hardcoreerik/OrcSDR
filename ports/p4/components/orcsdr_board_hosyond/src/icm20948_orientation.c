#include "icm20948_orientation.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ICM_ADDR 0x68
#define REG_BANK 0x7F
#define REG_WHO_AM_I 0x00
#define REG_PWR_MGMT_1 0x06
#define REG_ACCEL_XOUT_H 0x2D
#define REG_ACCEL_CONFIG 0x14
#define WHO_AM_I_VALUE 0xEA
#define ACCEL_LSB_PER_G 16384.0f /* datasheet sensitivity at +/-2 g */

static i2c_master_dev_handle_t s_device;
static uint8_t s_bank = 0xFF;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {reg, value};
    return i2c_master_transmit(s_device, tx, sizeof(tx), 100);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(s_device, &reg, 1, data, length, 100);
}

static esp_err_t select_bank(uint8_t bank)
{
    if (s_bank == bank) return ESP_OK;
    esp_err_t e = write_reg(REG_BANK, (uint8_t)(bank << 4));
    if (e == ESP_OK) s_bank = bank;
    return e;
}

static esp_err_t checked_write(uint8_t reg, uint8_t value)
{
    uint8_t actual = 0;
    esp_err_t e = write_reg(reg, value);
    if (e == ESP_OK) e = read_regs(reg, &actual, 1);
    return e != ESP_OK ? e : (actual == value ? ESP_OK : ESP_ERR_INVALID_RESPONSE);
}

esp_err_t icm20948_orientation_start(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    if (s_device) return ESP_OK;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ICM_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t e = i2c_master_bus_add_device(bus, &cfg, &s_device);
    if (e != ESP_OK) return e;
    s_bank = 0xFF;
    e = select_bank(0);
    uint8_t id = 0;
    if (e == ESP_OK) e = read_regs(REG_WHO_AM_I, &id, 1);
    if (e == ESP_OK && id != WHO_AM_I_VALUE) e = ESP_ERR_NOT_FOUND;
    if (e == ESP_OK) e = write_reg(REG_PWR_MGMT_1, 0x80); /* device reset */
    if (e == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_bank = 0xFF;
        e = select_bank(0);
    }
    if (e == ESP_OK) e = checked_write(REG_PWR_MGMT_1, 0x01); /* wake, auto clock */
    if (e == ESP_OK) e = select_bank(2);
    /* DLPF enabled (configuration 3), +/-2 g. Filtered, low-rate gravity is
       sufficient; the policy has its own temporal filter and shake rejection. */
    if (e == ESP_OK) e = checked_write(REG_ACCEL_CONFIG, 0x19);
    if (e == ESP_OK) e = select_bank(0);
    if (e != ESP_OK) icm20948_orientation_stop();
    return e;
}

esp_err_t icm20948_orientation_read(icm20948_orientation_sample_t *sample)
{
    if (!sample) return ESP_ERR_INVALID_ARG;
    if (!s_device) return ESP_ERR_INVALID_STATE;
    esp_err_t e = select_bank(0);
    uint8_t raw[6];
    if (e == ESP_OK) e = read_regs(REG_ACCEL_XOUT_H, raw, sizeof(raw));
    if (e != ESP_OK) return e;
    const int16_t x = (int16_t)((uint16_t)raw[0] << 8 | raw[1]);
    const int16_t y = (int16_t)((uint16_t)raw[2] << 8 | raw[3]);
    const int16_t z = (int16_t)((uint16_t)raw[4] << 8 | raw[5]);
    *sample = (icm20948_orientation_sample_t){x / ACCEL_LSB_PER_G, y / ACCEL_LSB_PER_G, z / ACCEL_LSB_PER_G};
    return ESP_OK;
}

void icm20948_orientation_stop(void)
{
    if (s_device) i2c_master_bus_rm_device(s_device);
    s_device = NULL;
    s_bank = 0xFF;
}
