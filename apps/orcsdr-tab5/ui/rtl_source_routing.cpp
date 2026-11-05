#include "rtl_source_routing.hpp"

#include "rtl_tcp_source.hpp"

namespace orcsdr::routing {
namespace {

namespace remote = orcsdr::rtl_tcp_source;

esp_err_t queued(bool ok) { return ok ? ESP_OK : ESP_ERR_NO_MEM; }

}  // namespace

esp_err_t set_tuner_gain_mode(esp_rtl_sdr_handle_t handle, esp_rtl_sdr_gain_mode_t mode) {
  if (remote::running()) return queued(remote::set_gain_manual(mode == ESP_RTL_SDR_GAIN_MODE_MANUAL));
  return ::esp_rtl_sdr_set_tuner_gain_mode(handle, mode);
}

esp_err_t set_tuner_gain(esp_rtl_sdr_handle_t handle, int gain_tenth_db) {
  if (remote::running())
    return queued(remote::set_gain_manual(true) && remote::set_gain_tenths_db(gain_tenth_db));
  return ::esp_rtl_sdr_set_tuner_gain(handle, gain_tenth_db);
}

esp_err_t set_rtl_agc(esp_rtl_sdr_handle_t handle, bool enable) {
  if (remote::running()) return queued(remote::set_agc(enable));
  return ::esp_rtl_sdr_set_rtl_agc(handle, enable);
}

}  // namespace orcsdr::routing
