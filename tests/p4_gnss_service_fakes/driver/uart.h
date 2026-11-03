#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef int uart_port_t;
#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_MAX 3
#define UART_PIN_NO_CHANGE -1
typedef enum { UART_DATA_8_BITS } uart_word_length_t;
typedef enum { UART_PARITY_DISABLE } uart_parity_t;
typedef enum { UART_STOP_BITS_1 } uart_stop_bits_t;
typedef enum { UART_HW_FLOWCTRL_DISABLE } uart_hw_flowcontrol_t;
typedef enum { UART_SCLK_DEFAULT } uart_sclk_t;
typedef struct { int baud_rate; uart_word_length_t data_bits; uart_parity_t parity; uart_stop_bits_t stop_bits; uart_hw_flowcontrol_t flow_ctrl; uint8_t rx_flow_ctrl_thresh; uart_sclk_t source_clk; } uart_config_t;
int uart_is_driver_installed(uart_port_t port);
esp_err_t uart_param_config(uart_port_t port, const uart_config_t* config);
esp_err_t uart_driver_install(uart_port_t port, int rx, int tx, int queue, void* event_queue, int flags);
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts);
esp_err_t uart_driver_delete(uart_port_t port);
int uart_read_bytes(uart_port_t port, void* buffer, uint32_t length, uint32_t ticks);
