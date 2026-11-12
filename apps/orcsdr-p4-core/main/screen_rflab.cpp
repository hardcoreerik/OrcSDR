// RF LAB (apps/orcsdr-tab5/ui/rf_lab.cpp and rf_analysis.cpp, unchanged) on the P4 console: live spectrum,
// measurements and the driver controls (rate, ppm, gain, AGC, bias tee, tuner bandwidth).
// Bias tee is applied only when the operator taps it in the lab; nothing else switches it.
#include <algorithm>
#include <cstring>
#include "esp_rtl_sdr.h"
#include "esp_timer.h"
#include "home_app.hpp"
#include "radio_service.hpp"
#include "rf_analysis.hpp"
#include "rf_lab.hpp"
#include "screens.hpp"

namespace {
namespace lab = orcsdr::rf_lab;
bool g_ready = false;
int64_t g_last_runtime_us = 0;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

lab::Runtime runtime() {
  lab::Runtime r{};
  const home_app::Status st = home_app::status();
  const auto rs = orcsdr::radio::snapshot();
  const orcsdr::radio::Gain g = orcsdr::radio::gain();
  const orcsdr::radio::Info inf = orcsdr::radio::info();
  r.frequency_hz = st.frequency_hz;
  r.sample_rate_sps = orcsdr::radio::sample_rate();
  r.capabilities = inf.caps;
  r.usb_overruns = inf.overruns;
  r.consumer_drops = inf.consumer_drops;
  r.effective_sps = rs.effective_sps;
  r.ppm = inf.ppm;
  r.gain_tenth_db = g.tenth_db;
  r.gain_count = g.step_count;
  for (int i = 0; i < g.step_count; ++i) r.gain_ladder[i] = g.steps[i];
  r.gain_mode = g.auto_mode ? lab::GainMode::automatic : lab::GainMode::manual;
  r.rtl_agc = g.rtl_agc;
  r.bias_tee = g.bias;
  r.tuner_bandwidth_requested_hz = inf.bw_requested_hz;
  r.tuner_bandwidth_applied_hz = inf.bw_applied_hz;
  r.tuner_bandwidth_count = inf.bw_count;
  for (int i = 0; i < inf.bw_count; ++i) r.tuner_bandwidths[i] = inf.bw_list[i];
  r.source_available = rs.receiver_state == 2;
  r.sound_enabled = st.sound;
  r.volume = st.volume;
  r.sd_writable = false;
  std::snprintf(r.driver_version, sizeof(r.driver_version), "%s", inf.version);
  std::snprintf(r.device, sizeof(r.device), "%s", inf.device[0] ? inf.device : "No RTL-SDR");
  std::snprintf(r.health, sizeof(r.health), "%s", rs.receiver_state == 2 ? inf.health : "not streaming");
  return r;
}

void iq_sink(const uint8_t* iq, size_t bytes) { orcsdr::rf_analysis::offer_iq(iq, bytes); }

void enter() {
  if (!g_ready) {
    g_ready = lab::initialize(nullptr);
    if (!g_ready) return;
  }
  lab::set_runtime(runtime());
  g_last_runtime_us = esp_timer_get_time();
  if (!lab::enter(0, 0)) {
    home_app::request_home();
    return;
  }
  orcsdr::radio::set_aux_iq_sink(iq_sink);
}
void leave() {
  orcsdr::radio::set_aux_iq_sink(nullptr);
  orcsdr::radio::set_sample_rate_override(0);
  if (lab::active()) lab::leave();
}

void tick() {
  if (!lab::active()) return;
  const int64_t now = esp_timer_get_time();
  if (now - g_last_runtime_us > 200000) {
    g_last_runtime_us = now;
    lab::set_runtime(runtime());
  }
  lab::service_ui(now_ms());
  lab::Action a{};
  while (lab::take_action(&a)) {
    switch (a.kind) {
      case lab::ActionKind::close: home_app::request_home(); break;
      case lab::ActionKind::tune_hz: home_app::request_frequency(static_cast<uint32_t>(a.value)); break;
      case lab::ActionKind::sample_rate_sps:
        if (esp_rtl_sdr_is_rate_supported(static_cast<uint32_t>(a.value)))
          orcsdr::radio::set_sample_rate_override(static_cast<uint32_t>(a.value));
        break;
      case lab::ActionKind::ppm: orcsdr::radio::set_ppm(a.value); break;
      case lab::ActionKind::gain_mode:
        if (a.value) orcsdr::radio::set_gain_manual(orcsdr::radio::gain().tenth_db);
        else orcsdr::radio::set_gain_auto();
        break;
      case lab::ActionKind::gain_tenth_db: orcsdr::radio::set_gain_manual(a.value); break;
      case lab::ActionKind::rtl_agc: orcsdr::radio::set_rtl_agc(a.value != 0); break;
      case lab::ActionKind::bias_tee: orcsdr::radio::set_bias_tee(a.value != 0); break;
      case lab::ActionKind::tuner_bandwidth_hz: orcsdr::radio::set_tuner_bandwidth(static_cast<uint32_t>(a.value)); break;
      default: break;
    }
  }
}

void touch(int x, int y, bool down) { lab::handle_touch(x, y, down, now_ms()); }
}  // namespace

namespace screens {
const Screen& rflab_screen() {
  static const Screen s{orcsdr::dashboards::Id::rf_lab, "RF_LAB", enter, leave, tick, nullptr, nullptr, nullptr, touch};
  return s;
}
}  // namespace screens
