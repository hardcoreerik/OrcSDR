#pragma once
#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_INPUT 2
#define GPIO_IS_VALID_GPIO(pin) ((pin)>=0 && (pin)<55)
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) GPIO_IS_VALID_GPIO(pin)
#include <stdint.h>
typedef int gpio_num_t;
typedef struct {uint64_t pin_bit_mask;int mode;} gpio_config_t;
int gpio_config(const gpio_config_t*);
int gpio_get_level(int);
int gpio_set_direction(int,int);
int gpio_set_level(int,int);
