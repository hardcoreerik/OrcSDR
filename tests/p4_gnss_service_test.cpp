#include "gnss_service.hpp"
#include "orcsdr_gnss_parser.h"
#include "freertos/task.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
int installed = 0, install_result = ESP_OK, param_result = ESP_OK, pin_result = ESP_OK;
int delete_result = ESP_OK, task_result = pdPASS, read_error_after_data = 0;
int read_calls = 0, delete_calls = 0, delay_calls = 0, tx_pin = 0, rx_pin = 0;
int64_t now_us = 1000000;
std::string input;
void (*task_entry)(void*) = nullptr;

std::string nmea(const char* body) {
  unsigned char sum = 0;
  for (const char* p = body; *p; ++p) sum ^= static_cast<unsigned char>(*p);
  char tail[8]; std::snprintf(tail, sizeof(tail), "*%02X\r\n", sum);
  return std::string("$") + body + tail;
}
void run_worker_once() {
  try { task_entry(nullptr); } catch (const std::runtime_error&) {}
}
}

int uart_is_driver_installed(uart_port_t) { return installed; }
esp_err_t uart_param_config(uart_port_t, const uart_config_t*) { return param_result; }
esp_err_t uart_driver_install(uart_port_t, int, int tx, int, void*, int) {
  assert(tx == 0); if (install_result == ESP_OK) installed = 1; return install_result;
}
esp_err_t uart_set_pin(uart_port_t, int tx, int rx, int, int) {
  tx_pin = tx; rx_pin = rx; return pin_result;
}
esp_err_t uart_driver_delete(uart_port_t) { ++delete_calls; if (delete_result == ESP_OK) installed = 0; return delete_result; }
int uart_read_bytes(uart_port_t, void* out, uint32_t cap, uint32_t) {
  ++read_calls;
  if (!input.empty()) {
    const size_t n = input.size() < cap ? input.size() : cap;
    std::memcpy(out, input.data(), n); input.erase(0, n); return static_cast<int>(n);
  }
  if (read_error_after_data > 0) { --read_error_after_data; return -1; }
  throw std::runtime_error("one worker pass complete");
}
int64_t esp_timer_get_time(void) { return now_us; }
BaseType_t xTaskCreate(void (*entry)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t* handle) {
  if (task_result != pdPASS) return task_result;
  task_entry = entry; *handle = reinterpret_cast<void*>(1); return pdPASS;
}
void vTaskDelay(unsigned) { ++delay_calls; }

int main() {
  assert(!gnss_service::start(UART_NUM_1, 23, 0));
  assert(gnss_service::status().last_error == ESP_ERR_INVALID_ARG);
  assert(!gnss_service::start(UART_NUM_1, 23, static_cast<uint32_t>(INT_MAX) + 1));
  installed = 1;
  assert(!gnss_service::start(UART_NUM_1, 23, 115200));
  assert(gnss_service::status().last_error == ESP_ERR_INVALID_STATE && delete_calls == 0);
  installed = 0;
  param_result = ESP_FAIL;
  assert(!gnss_service::start(UART_NUM_1, 23, 115200));
  assert(gnss_service::status().last_error == ESP_FAIL);
  param_result = ESP_OK; install_result = ESP_FAIL;
  assert(!gnss_service::start(UART_NUM_1, 23, 115200));
  install_result = ESP_OK; pin_result = ESP_FAIL; delete_result = ESP_ERR_INVALID_STATE;
  assert(!gnss_service::start(UART_NUM_1, 23, 115200));
  assert(gnss_service::status().cleanup_error == ESP_ERR_INVALID_STATE && delete_calls == 1);
  installed = 0; delete_result = ESP_OK; pin_result = ESP_OK; task_result = 0;
  assert(!gnss_service::start(UART_NUM_1, 23, 115200));
  assert(gnss_service::status().cleanup_error == ESP_OK && !installed);

  task_result = pdPASS;
  assert(gnss_service::start(UART_NUM_1, 23, 115200));
  assert(installed && tx_pin == UART_PIN_NO_CHANGE && rx_pin == 23);
  assert(gnss_service::start(UART_NUM_1, 23, 115200));
  assert(!gnss_service::start(UART_NUM_1, 22, 115200));

  input = nmea("GPRMC,123519,A,4807.038,N,01131.000,E,0.0,0.0,230394,,,A") +
          nmea("GPGGA,120001,,,,,0,00,99.9,,,,,,");
  run_worker_once();
  auto s = gnss_service::status();
  assert(s.bytes_read > 0 && s.fixes == 1 && s.no_fixes == 1);
  assert(s.last_valid_fix_available && !s.current_fix_usable);
  assert(s.latitude_e7 == 481173000 && s.longitude_e7 == 115166667 && s.fix_age_ms == 0);
  now_us += 1000000;
  assert(gnss_service::status().fix_age_ms == 1000); // age keeps advancing after NO_FIX

  input = nmea("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,230394,,,A");
  now_us += 1000000;
  run_worker_once();
  s = gnss_service::status();
  assert(s.current_fix_usable && s.fix_age_ms == 0);
  read_error_after_data = 1;
  run_worker_once();
  s = gnss_service::status();
  assert(!s.current_fix_usable && s.last_valid_fix_available && s.read_errors == 1 && delay_calls == 1);

  input = nmea("GPRMC,123519,A,4807.038,N,01131.000,E,0,0,230394,,,A");
  now_us += 1000000;
  run_worker_once();
  s = gnss_service::status();
  assert(s.current_fix_usable && s.fix_age_ms == 0);
  now_us += static_cast<int64_t>(gnss_service::kFixFreshnessMs) * 1000;
  now_us -= 1000;
  s = gnss_service::status();
  assert(s.current_fix_usable && s.fix_age_ms == gnss_service::kFixFreshnessMs - 1);
  now_us += 1000;
  s = gnss_service::status();
  assert(!s.current_fix_usable && s.last_valid_fix_available && s.fix_age_ms == gnss_service::kFixFreshnessMs);
  now_us += 7000;
  assert(gnss_service::status().fix_age_ms == gnss_service::kFixFreshnessMs + 7);
  std::puts("P4_GNSS_SERVICE_TEST pass");
}
