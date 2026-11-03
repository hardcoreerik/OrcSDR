#include "gnss_service.hpp"

#include <atomic>
#include <climits>
#include "orcsdr_gnss_parser.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace gnss_service {
namespace {
constexpr size_t kReadChunk = 64;
constexpr uint32_t kPollMs = 100;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
Status g_status;
std::atomic<bool> g_starting{false};
orcsdr_gnss_parser_t g_parser{};
int64_t g_last_fix_us = 0;

void worker(void*) {
  uint8_t bytes[kReadChunk];
  for (;;) {
    const Status config = status();
    const int count = uart_read_bytes(config.uart_port, bytes, sizeof(bytes), pdMS_TO_TICKS(kPollMs));
    const int64_t now = esp_timer_get_time();
    if (count < 0) {
      orcsdr_gnss_parser_clear(&g_parser);
      portENTER_CRITICAL(&g_lock);
      ++g_status.read_errors;
      g_status.last_error = ESP_FAIL;
      g_status.current_fix_usable = false;
      portEXIT_CRITICAL(&g_lock);
      vTaskDelay(pdMS_TO_TICKS(kPollMs));
    } else if (count > 0) {
      portENTER_CRITICAL(&g_lock);
      g_status.bytes_read += static_cast<uint32_t>(count);
      portEXIT_CRITICAL(&g_lock);
      for (int i = 0; i < count; ++i) {
        orcsdr_gnss_result_t parsed{};
        const auto result = orcsdr_gnss_parser_feed(&g_parser, bytes[i], &parsed);
        if (result == ORCSDR_GNSS_INCOMPLETE) continue;
        portENTER_CRITICAL(&g_lock);
        ++g_status.sentences;
        if (result == ORCSDR_GNSS_FIX && parsed.fix_valid) {
          ++g_status.fixes;
          g_status.last_valid_fix_available = true;
          g_status.current_fix_usable = true;
          g_status.latitude_e7 = parsed.latitude_e7;
          g_status.longitude_e7 = parsed.longitude_e7;
          g_status.fix_age_ms = 0;
          g_last_fix_us = now;
        } else if (result == ORCSDR_GNSS_NO_FIX) {
          ++g_status.no_fixes;
          g_status.current_fix_usable = false;
        } else if (result == ORCSDR_GNSS_REJECTED || result == ORCSDR_GNSS_FAILURE) {
          ++g_status.rejected;
        } else if (result == ORCSDR_GNSS_IGNORED) {
          ++g_status.ignored;
        }
        portEXIT_CRITICAL(&g_lock);
      }
    }
    portENTER_CRITICAL(&g_lock);
    if (g_status.last_valid_fix_available) {
      const uint64_t age_ms = static_cast<uint64_t>(now - g_last_fix_us) / 1000;
      g_status.fix_age_ms = age_ms > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(age_ms);
      if (age_ms >= kFixFreshnessMs) g_status.current_fix_usable = false;
    }
    portEXIT_CRITICAL(&g_lock);
  }
}
} // namespace

Status status() {
  portENTER_CRITICAL(&g_lock);
  Status copy = g_status;
  if (copy.last_valid_fix_available) {
    const uint64_t age = static_cast<uint64_t>(esp_timer_get_time() - g_last_fix_us) / 1000;
    copy.fix_age_ms = age > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(age);
    if (age >= kFixFreshnessMs) copy.current_fix_usable = false;
  }
  portEXIT_CRITICAL(&g_lock);
  return copy;
}

bool start(uart_port_t port, int rx_gpio, uint32_t baud) {
  if (g_starting.exchange(true)) return false;
  const Status existing = status();
  if (existing.started) {
    const bool same = existing.uart_port == port && existing.rx_gpio == rx_gpio && existing.baud == baud;
    g_starting = false;
    return same;
  }
  auto fail = [](esp_err_t error) {
    portENTER_CRITICAL(&g_lock);
    g_status.last_error = error;
    portEXIT_CRITICAL(&g_lock);
    g_starting = false;
    return false;
  };
  if (port < UART_NUM_0 || port >= UART_NUM_MAX || rx_gpio < 0 || baud == 0 || baud > INT_MAX)
    return fail(ESP_ERR_INVALID_ARG);
  if (uart_is_driver_installed(port)) return fail(ESP_ERR_INVALID_STATE);

  uart_config_t config{};
  config.baud_rate = static_cast<int>(baud);
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  esp_err_t err = uart_param_config(port, &config);
  if (err != ESP_OK) return fail(err);
  err = uart_driver_install(port, 2048, 0, 0, nullptr, 0);
  if (err != ESP_OK) return fail(err);
  err = uart_set_pin(port, UART_PIN_NO_CHANGE, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    const esp_err_t cleanup = uart_driver_delete(port);
    if (cleanup != ESP_OK) {
      portENTER_CRITICAL(&g_lock);
      g_status.cleanup_error = cleanup;
      portEXIT_CRITICAL(&g_lock);
    }
    return fail(err);
  }

  orcsdr_gnss_parser_init(&g_parser);
  g_last_fix_us = 0;
  portENTER_CRITICAL(&g_lock);
  g_status.uart_port = port;
  g_status.rx_gpio = rx_gpio;
  g_status.baud = baud;
  portEXIT_CRITICAL(&g_lock);
  TaskHandle_t task = nullptr;
  if (xTaskCreate(worker, "gnss_uart", 3072, nullptr, 1, &task) != pdPASS) {
    const esp_err_t cleanup = uart_driver_delete(port);
    portENTER_CRITICAL(&g_lock);
    g_status.uart_port = UART_NUM_MAX;
    g_status.rx_gpio = -1;
    g_status.baud = 0;
    g_status.cleanup_error = cleanup;
    portEXIT_CRITICAL(&g_lock);
    return fail(ESP_ERR_NO_MEM);
  }
  portENTER_CRITICAL(&g_lock);
  g_status.started = true;
  g_status.last_error = ESP_OK;
  portEXIT_CRITICAL(&g_lock);
  g_starting = false;
  return true;
}
} // namespace gnss_service
