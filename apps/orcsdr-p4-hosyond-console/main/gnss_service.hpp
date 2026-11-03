#pragma once

#include <cstdint>
#include "esp_err.h"
#include "driver/uart.h"

namespace gnss_service {
// Passive, process-lifetime UART reader. Baud and RX pin are supplied by the
// caller because the installed receiver's output baud has not been qualified.
constexpr uint32_t kFixFreshnessMs = 5000;

struct Status {
  bool started = false;
  bool current_fix_usable = false;
  bool last_valid_fix_available = false;
  uart_port_t uart_port = UART_NUM_MAX;
  int rx_gpio = -1;
  uint32_t baud = 0;
  uint32_t bytes_read = 0;
  uint32_t sentences = 0;
  uint32_t fixes = 0;
  uint32_t no_fixes = 0;
  uint32_t rejected = 0;
  uint32_t ignored = 0;
  uint32_t read_errors = 0;
  esp_err_t cleanup_error = ESP_OK;
  uint32_t fix_age_ms = UINT32_MAX;
  int32_t latitude_e7 = 0;
  int32_t longitude_e7 = 0;
  esp_err_t last_error = ESP_OK;
};

// Idempotent for the same configuration. Refuses an already installed UART;
// never reconfigures or removes a driver's UART that it does not own.
bool start(uart_port_t port, int rx_gpio, uint32_t baud);
Status status();
} // namespace gnss_service
