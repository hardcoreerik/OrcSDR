#include "rtl_source_routing.hpp"

#include "rtl_tcp_protocol.hpp"
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

esp_err_t get_tuner_gain_mode(esp_rtl_sdr_handle_t handle, esp_rtl_sdr_gain_mode_t* out_mode) {
  if (!remote::running()) return ::esp_rtl_sdr_get_tuner_gain_mode(handle, out_mode);
  if (out_mode == nullptr) return ESP_ERR_INVALID_ARG;
  const auto g = remote::remote_gain();
  *out_mode = (g.mode_known && g.manual) ? ESP_RTL_SDR_GAIN_MODE_MANUAL : ESP_RTL_SDR_GAIN_MODE_AUTO;   // the server starts in tuner AGC
  return ESP_OK;
}

esp_err_t get_tuner_gain(esp_rtl_sdr_handle_t handle, int* out_gain_tenth_db) {
  if (!remote::running()) return ::esp_rtl_sdr_get_tuner_gain(handle, out_gain_tenth_db);
  if (out_gain_tenth_db == nullptr) return ESP_ERR_INVALID_ARG;
  const auto g = remote::remote_gain();
  *out_gain_tenth_db = g.gain_known ? static_cast<int>(g.gain_tenths_db) : 0;
  return ESP_OK;
}

esp_err_t get_tuner_gains(esp_rtl_sdr_handle_t handle, int* out_gains_tenth_db, size_t max_count, size_t* out_count) {
  if (!remote::running()) return ::esp_rtl_sdr_get_tuner_gains(handle, out_gains_tenth_db, max_count, out_count);
  if (out_count == nullptr) return ESP_ERR_INVALID_ARG;
  const auto g = remote::remote_gain();
  *out_count = orcsdr::rtl_tcp::nominal_gain_steps(g.tuner_type_raw, g.gain_count, out_gains_tenth_db, max_count);
  return ESP_OK;
}

esp_err_t get_rtl_agc(esp_rtl_sdr_handle_t handle, bool* out_enable) {
  if (!remote::running()) return ::esp_rtl_sdr_get_rtl_agc(handle, out_enable);
  if (out_enable == nullptr) return ESP_ERR_INVALID_ARG;
  const auto g = remote::remote_gain();
  *out_enable = g.agc_known && g.agc_on;
  return ESP_OK;
}

}  // namespace orcsdr::routing
