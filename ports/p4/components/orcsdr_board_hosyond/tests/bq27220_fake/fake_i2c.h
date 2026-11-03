#pragma once
#include <stdint.h>
#include "driver/i2c_master.h"

void fake_i2c_reset(void);
void fake_i2c_set_word(uint8_t command, uint16_t value);
void fake_i2c_fail_transfer(unsigned transfer_index, esp_err_t error);
void fake_i2c_fail_add(esp_err_t error);
unsigned fake_i2c_transfer_count(void);
unsigned fake_i2c_remove_count(void);
uint16_t fake_i2c_last_address(void);
uint32_t fake_i2c_last_speed(void);
