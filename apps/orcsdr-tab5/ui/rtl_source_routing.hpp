#pragma once

// Gain controls that follow the active receiver. While a remote rtl_tcp stream
// runs, a gain change is sent to that server; otherwise it goes to the USB driver
// exactly as before. The dashboards keep calling the driver's names; main.cpp
// redirects those three names here, so no dashboard needs a second code path.
//
// rtl_tcp never acknowledges a command: ESP_OK from the remote branch means the
// command was queued, not that the remote hardware applied it. The driver's
// getters are not rerouted and describe the USB dongle's shadow state only.

#include <esp_err.h>
#include <esp_rtl_sdr.h>

namespace orcsdr::routing {

esp_err_t set_tuner_gain_mode(esp_rtl_sdr_handle_t handle, esp_rtl_sdr_gain_mode_t mode);
esp_err_t set_tuner_gain(esp_rtl_sdr_handle_t handle, int gain_tenth_db);  // forces manual, like the driver
esp_err_t set_rtl_agc(esp_rtl_sdr_handle_t handle, bool enable);

}  // namespace orcsdr::routing
