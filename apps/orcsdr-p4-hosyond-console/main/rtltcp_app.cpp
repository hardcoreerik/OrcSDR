#include "rtltcp_app.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include "commands.hpp"
#include "esp_log.h"
#include "esp_rtl_sdr.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "eth_app.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_store.hpp"
#include "radio_service.hpp"
#include "rtl_tcp_proto.hpp"
#include "web_control.hpp"
#include "wifi_app.hpp"

namespace rtltcp_app {
namespace {
constexpr char TAG[] = "RTLTCP_APP";
constexpr int kReceiver = 0;
constexpr uint32_t kQueueBytes = 4 * 1024 * 1024;  // about 0.9 s at 2.4 MS/s; lives in PSRAM; a client that falls behind loses whole blocks, counted

std::atomic<bool> g_enabled{false}, g_control{false};
std::atomic<uint32_t> g_sink_drops{0};

// esp_rtl_sdr_profile_t value -> rtl_tcp's tuner enum (the two enums differ).
uint32_t rtl_tcp_tuner(uint8_t profile) {
  switch (profile) {
    case 1:  // Blog V4: R828D
    case 4:  // Blog V4L: R828S, closest family is R828D
      return esp_rtl_tcp::proto::kTunerR828D;
    case 2:  // Blog V3: R820T2
    case 3:  // NESDR SMArt v5: R820-family
      return esp_rtl_tcp::proto::kTunerR820T;
    default: return esp_rtl_tcp::proto::kTunerUnknown;
  }
}

// The rates the driver can run, after its own quantisation (design rule 2): 225,001 to 300,000 and 900,001 to 3,200,000 S/s.
bool driver_rate_ok(uint32_t sps) { return esp_rtl_sdr_is_rate_supported(sps); }

// What the active link can carry for one stream, Mbit/s. Provisional: the Ethernet figure leaves headroom on a 100 Mbit link, the Wi-Fi figure is
// the measured sustained rate of the SDIO-attached coprocessor path (COM8: about 5 Mbit/s of plain HTTP download, bursts to 16), so a request above it ends the session with a recorded reason instead of dropping silently.
uint32_t link_budget_mbit() {
  if (eth_app::status().got_ip) return 85;
  const wifi_app::Status w = wifi_app::status();
  return w.connected ? 5 : 0;
}

void on_block(const orcsdr::radio::IqBlock& b, void*) { esp_rtl_tcp::push(b.data, b.bytes, b.sample_rate, b.center_hz); }

class OrcHost : public esp_rtl_tcp::Host {
 public:
  uint32_t tuner_type() override { return rtl_tcp_tuner(orcsdr::radio::info(kReceiver).profile); }

  uint32_t gain_count() override {
    // One ladder for the whole session: the index in command 0x0D means the same step until the client disconnects.
    const orcsdr::radio::Gain g = orcsdr::radio::gain(kReceiver);
    steps_count_ = g.available ? g.step_count : 0;
    std::copy(g.steps, g.steps + steps_count_, steps_);
    return steps_count_;
  }

  bool session_begin() override {
    orcsdr::radio::ReceiverInfo ri;
    if (!orcsdr::radio::receiver_info(kReceiver, &ri) || ri.state != 2) return false;  // no receiver streaming: nothing to serve
    orcsdr::radio::SinkSpec spec;
    spec.name = "rtltcp";
    spec.fn = on_block;
    spec.kind = orcsdr::radio::SinkKind::queued;
    spec.receiver = kReceiver;
    spec.queue_bytes = kQueueBytes;
    spec.task_core = 0;
    spec.task_priority = 5;  // above the screen task (4): on a board with a display it would otherwise starve this task
    // Wi-Fi modem sleep adds tens of milliseconds of latency and caps a TCP stream; a streaming session turns it off and restores it at the end.
    if (wifi_app::status().connected && esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK) wifi_ps_off_ = true;
    sink_ = orcsdr::radio::add_sink(spec);
    return sink_ >= 0;
  }

  void session_end() override {
    if (sink_ < 0) return;
    orcsdr::radio::SinkStats ss;
    if (orcsdr::radio::sink_stats(sink_, &ss)) g_sink_drops += ss.drops;
    if (!orcsdr::radio::remove_sink(sink_)) ESP_LOGW(TAG, "RTLTCP_SINK_QUARANTINED the receiver service could not stop the sink cleanly");
    sink_ = -1;
    if (wifi_ps_off_) {
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
      wifi_ps_off_ = false;
    }
    if (rate_overridden_) {
      orcsdr::radio::set_sample_rate_override(0);  // back to the console's own rate policy
      rate_overridden_ = false;
    }
  }

  // Blocks the receiver service has dropped for the connected client so far (live, unlike the total added at session end).
  uint32_t live_drops() const {
    orcsdr::radio::SinkStats ss;
    const orcsdr::radio::SinkHandle h = sink_;
    return h >= 0 && orcsdr::radio::sink_stats(h, &ss) ? ss.drops : 0;
  }

  bool set_center(uint32_t hz) override {
    if (hz < web_control::tune_min_hz(kReceiver) || hz > ESP_RTL_SDR_FREQ_MAX_HZ) return false;
    orcsdr::radio::tune_to(kReceiver, hz);
    return true;
  }

  esp_rtl_tcp::RateResult set_rate(uint32_t sps) override {
    if (!driver_rate_ok(sps)) return esp_rtl_tcp::RateResult::unsupported;
    if (static_cast<uint64_t>(sps) * 16 > static_cast<uint64_t>(link_budget_mbit()) * 1000000ull) return esp_rtl_tcp::RateResult::exceeds_link;
    orcsdr::radio::set_sample_rate_override(sps);  // the exact rate, not the console's 960 kS/s / 2.4 MS/s pair
    rate_overridden_ = true;
    return esp_rtl_tcp::RateResult::ok;
  }

  bool set_gain_mode(bool manual) override {
    const orcsdr::radio::Gain g = orcsdr::radio::gain(kReceiver);
    if (!manual) {
      if (!g.auto_available) return false;
      orcsdr::radio::set_gain_auto(kReceiver);
      return true;
    }
    if (!g.available) return false;
    orcsdr::radio::set_gain_manual(kReceiver, g.tenth_db);
    return true;
  }

  bool set_gain_tenth_db(int tenth) override {
    if (steps_count_ == 0) return false;
    int best = steps_[0];
    for (uint32_t i = 1; i < steps_count_; ++i)
      if (std::abs(steps_[i] - tenth) < std::abs(best - tenth)) best = steps_[i];  // nearest ladder step; this forces manual mode
    orcsdr::radio::set_gain_manual(kReceiver, best);
    return true;
  }

  bool set_gain_index(uint32_t index) override {
    if (index >= steps_count_) return false;
    orcsdr::radio::set_gain_manual(kReceiver, steps_[index]);
    return true;
  }

  bool set_ppm(int ppm) override {
    orcsdr::radio::set_ppm(ppm);
    return true;
  }

  bool set_rtl_agc(bool on) override {
    if (!orcsdr::radio::gain(kReceiver).rtl_agc_available) return false;
    orcsdr::radio::set_rtl_agc(kReceiver, on);
    return true;
  }

  bool set_bias_tee(bool on) override {
    if (!orcsdr::radio::gain(kReceiver).bias_available) return false;
    orcsdr::radio::set_bias_tee(kReceiver, on);
    return true;
  }

 private:
  uint32_t steps_count_ = 0;
  int steps_[32] = {};
  volatile orcsdr::radio::SinkHandle sink_ = -1;
  bool rate_overridden_ = false;
  bool wifi_ps_off_ = false;
};

OrcHost g_host;

bool have_address() {
  const wifi_app::Status w = wifi_app::status();
  return (w.connected && w.ip[0]) || eth_app::status().got_ip;
}

// Serves while enabled and the board has an address; restarts the server when the mode changes.
void manager(void*) {
  bool running_control = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    const bool want = g_enabled.load() && have_address();
    const bool control = g_control.load();
    if (esp_rtl_tcp::running() && (!want || control != running_control)) esp_rtl_tcp::stop();
    if (want && !esp_rtl_tcp::running()) {
      esp_rtl_tcp::Config cfg;
      cfg.allow_control = control;
      if (esp_rtl_tcp::start(cfg, g_host)) running_control = control;
    }
  }
}
}  // namespace

void start() {
  orcsdr::NvsStore s;
  if (s.begin("orcsdr_rtltcp", true)) {
    g_enabled = s.get_bool("enabled", false);  // off unless the owner turned it on
    g_control = s.get_bool("control", false);  // listen-only unless the owner turned control on
    s.end();
  }
  xTaskCreatePinnedToCore(manager, "rtltcp_mgr", 4096, nullptr, 2, nullptr, 0);
}

static void save(const char* key, bool value) {
  orcsdr::NvsStore s;
  if (s.begin("orcsdr_rtltcp", false)) {
    s.put_bool(key, value);
    s.end();
  }
}

void set_enabled(bool on) {
  g_enabled = on;
  save("enabled", on);
}

void set_control(bool on) {
  g_control = on;
  save("control", on);
}

Status status() {
  Status s;
  s.enabled = g_enabled.load();
  s.control = g_control.load();
  s.receiver = kReceiver;
  s.port = esp_rtl_tcp::Config{}.port;
  s.server = esp_rtl_tcp::stats();
  s.sink_blocks_dropped = g_sink_drops.load() + g_host.live_drops();
  return s;
}

}  // namespace rtltcp_app
