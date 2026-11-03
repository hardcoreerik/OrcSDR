#include "fake_i2c.h"

#include <stdlib.h>
#include <string.h>

struct fake_i2c_device { int active; };
static struct fake_i2c_device s_device;
static uint16_t s_words[256];
static unsigned s_transfer_count;
static unsigned s_fail_transfer;
static unsigned s_remove_count;
static esp_err_t s_transfer_error;
static esp_err_t s_add_error;
static uint16_t s_address;
static uint32_t s_speed;

void fake_i2c_reset(void)
{
    memset(s_words, 0, sizeof(s_words));
    s_transfer_count = s_fail_transfer = s_remove_count = 0;
    s_transfer_error = ESP_OK;
    s_add_error = ESP_OK;
    s_address = 0;
    s_speed = 0;
    s_device.active = 0;
}

void fake_i2c_set_word(uint8_t command, uint16_t value) { s_words[command] = value; }
void fake_i2c_fail_transfer(unsigned n, esp_err_t error) { s_fail_transfer = n; s_transfer_error = error; }
void fake_i2c_fail_add(esp_err_t error) { s_add_error = error; }
unsigned fake_i2c_transfer_count(void) { return s_transfer_count; }
unsigned fake_i2c_remove_count(void) { return s_remove_count; }
uint16_t fake_i2c_last_address(void) { return s_address; }
uint32_t fake_i2c_last_speed(void) { return s_speed; }

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *out_device)
{
    if (!bus || !config || !out_device) return ESP_ERR_INVALID_ARG;
    if (s_add_error != ESP_OK) return s_add_error;
    s_address = config->device_address;
    s_speed = config->scl_speed_hz;
    s_device.active = 1;
    *out_device = &s_device;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    if (device != &s_device || !s_device.active) return ESP_ERR_INVALID_ARG;
    s_device.active = 0;
    ++s_remove_count;
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *write_buffer,
                                      size_t write_size, uint8_t *read_buffer, size_t read_size,
                                      int timeout_ms)
{
    ++s_transfer_count;
    if (s_fail_transfer == s_transfer_count) return s_transfer_error;
    if (device != &s_device || !s_device.active || !write_buffer || write_size != 1 ||
        !read_buffer || read_size != 2 || timeout_ms != 100) return ESP_ERR_INVALID_ARG;
    const uint16_t value = s_words[write_buffer[0]];
    read_buffer[0] = (uint8_t)value;
    read_buffer[1] = (uint8_t)(value >> 8);
    return ESP_OK;
}
