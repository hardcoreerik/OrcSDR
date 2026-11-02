#pragma once
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
inline esp_err_t orcsdr_board_c6_enable(bool) { return ESP_OK; }
