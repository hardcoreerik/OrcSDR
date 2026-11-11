#include "rtl_tcp_server.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include "nvs_store.hpp"
#include "rtl_tcp_iq_ring.hpp"

namespace orcsdr::rtl_tcp_server {
namespace {

using rtl_tcp::Command;
using rtl_tcp::RawCommand;
using rtl_tcp::ServerState;

constexpr size_t kRingBytes = 256 * 1024;      // PSRAM; about 130 ms at 1 MS/s
constexpr uint32_t kDefaultRate = 2048000;     // what a stock rtl_tcp starts at
constexpr uint32_t kDefaultFrequency = 100000000;
constexpr uint32_t kStopTimeoutMs = 2000;
constexpr UBaseType_t kTaskPriority = 4;
constexpr uint32_t kTaskStackBytes = 8192;
constexpr const char* kNvsNamespace = "rtltcpd";
constexpr const char* kTag = "rtl_tcpd";

Hooks g_hooks{};
rtl_tcp::ServerStats g_stats;
rtl_tcp::IqRing g_ring;
uint8_t* g_ring_storage = nullptr;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_stream_to_ring{false};  // read by the driver callback
std::atomic<bool> g_client_active{false};
std::atomic<uint8_t> g_state{static_cast<uint8_t>(ServerState::stopped)};
std::atomic<uint32_t> g_rate{kDefaultRate};
std::atomic<uint32_t> g_frequency{kDefaultFrequency};
std::atomic<uint32_t> g_unsupported{0};
std::atomic<uint32_t> g_errors{0};
char g_bind[20] = "";
uint16_t g_port = rtl_tcp::kDefaultPort;
TaskHandle_t g_task = nullptr;
bool g_begun = false;

bool stream_running = false;  // server task only
bool g_resume_local = false;  // a local stream was running when the client arrived

// The client owns the dongle: stop any local stream, block local starts, and
// give it all back (resuming what was running) when the client leaves.
bool acquire_receiver() {
  g_hooks.allow_local_start(false);
  g_resume_local = g_hooks.local_stream_busy();
  if (g_resume_local) {
    g_hooks.request_local_stop();
    for (int waited = 0; g_hooks.local_stream_busy() && waited < 4000; waited += 20)
      vTaskDelay(pdMS_TO_TICKS(20));
    if (g_hooks.local_stream_busy()) {
      g_hooks.allow_local_start(true);
      return false;  // the local stream would not stop; leave it alone
    }
  }
  return true;
}

void release_receiver() {
  g_hooks.allow_local_start(true);
  if (g_resume_local) {
    g_resume_local = false;
    g_hooks.resume_local();
  }
}

// The tuner state the local app had before a remote client arrived. A client's gain, AGC and ppm commands are not its own:
// they change the shared dongle, so they are put back when it leaves.
struct SavedTuner {
  bool valid = false;
  esp_rtl_sdr_gain_mode_t gain_mode = ESP_RTL_SDR_GAIN_MODE_AUTO;
  int gain_tenth_db = 0;
  bool rtl_agc = false;
  int ppm = 0;
};
SavedTuner g_saved_tuner;

void save_tuner() {
  const auto handle = g_hooks.handle();
  SavedTuner s;
  s.valid = esp_rtl_sdr_get_tuner_gain_mode(handle, &s.gain_mode) == ESP_OK;
  (void)esp_rtl_sdr_get_tuner_gain(handle, &s.gain_tenth_db);
  (void)esp_rtl_sdr_get_rtl_agc(handle, &s.rtl_agc);
  (void)esp_rtl_sdr_get_freq_correction(handle, &s.ppm);
  g_saved_tuner = s;
}

void restore_tuner() {
  if (!g_saved_tuner.valid) return;
  const auto handle = g_hooks.handle();
  (void)esp_rtl_sdr_set_freq_correction(handle, g_saved_tuner.ppm);
  (void)esp_rtl_sdr_set_rtl_agc(handle, g_saved_tuner.rtl_agc);
  if (g_saved_tuner.gain_mode == ESP_RTL_SDR_GAIN_MODE_MANUAL)
    (void)esp_rtl_sdr_set_tuner_gain(handle, g_saved_tuner.gain_tenth_db);
  (void)esp_rtl_sdr_set_tuner_gain_mode(handle, g_saved_tuner.gain_mode);
  g_saved_tuner.valid = false;
}

bool start_stream() {
  esp_rtl_sdr_stream_config_t st;
  esp_rtl_sdr_stream_config_default(&st);
  st.preset = ESP_RTL_SDR_PRESET_CUSTOM_HZ;
  st.frequency_hz = g_frequency.load();
  st.sample_rate_sps = g_rate.load();
  g_ring.reset();
  g_stream_to_ring.store(true, std::memory_order_release);
  const esp_err_t err = esp_rtl_sdr_start(g_hooks.handle(), &st);
  if (err != ESP_OK) {
    g_stream_to_ring.store(false, std::memory_order_release);
    ESP_LOGW(kTag, "RTL_TCPD_START_FAILED %s rate=%u freq=%u", esp_rtl_sdr_err_to_name(err),
             static_cast<unsigned>(st.sample_rate_sps), static_cast<unsigned>(st.frequency_hz));
    return false;
  }
  stream_running = true;
  return true;
}

void stop_stream() {
  g_stream_to_ring.store(false, std::memory_order_release);  // callback stops writing first
  if (stream_running) (void)esp_rtl_sdr_stop(g_hooks.handle(), kStopTimeoutMs);
  stream_running = false;
}

class Backend final : public rtl_tcp::ServerBackend {
 public:
  rtl_tcp::Greeting greeting() override {
    rtl_tcp::Greeting g;
    const auto profile = esp_rtl_sdr_get_profile(g_hooks.handle());
    g.tuner_type_raw = profile == ESP_RTL_SDR_PROFILE_BLOG_V4 ? 6u
                       : profile == ESP_RTL_SDR_PROFILE_E4000 ? 1u
                                                              : 5u;
    size_t count = 0;
    g.gain_count = esp_rtl_sdr_get_tuner_gains(g_hooks.handle(), nullptr, 0, &count) == ESP_OK
                       ? static_cast<uint32_t>(count)
                       : 29u;
    return g;
  }

  bool client_connected() override {
    if (!g_hooks.usb_ready()) return false;
    if (!acquire_receiver()) return false;
    if (g_ring_storage == nullptr) {
      g_ring_storage = static_cast<uint8_t*>(
          heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (g_ring_storage == nullptr) {
        release_receiver();
        return false;
      }
      g_ring.init(g_ring_storage, kRingBytes);
    }
    g_rate.store(kDefaultRate);
    g_frequency.store(kDefaultFrequency);
    save_tuner();
    if (!start_stream()) {
      release_receiver();
      return false;
    }
    g_client_active.store(true, std::memory_order_release);
    return true;
  }

  void client_disconnected() override {
    stop_stream();
    restore_tuner();
    g_client_active.store(false, std::memory_order_release);
    release_receiver();
  }

  void apply(const RawCommand& command) override {
    const auto handle = g_hooks.handle();
    esp_err_t err = ESP_OK;
    switch (command.id) {
      case static_cast<uint8_t>(Command::set_frequency):
        g_frequency.store(command.parameter);
        if (stream_running) err = esp_rtl_sdr_retune_hz(handle, command.parameter);
        break;
      case static_cast<uint8_t>(Command::set_sample_rate):
        if (!esp_rtl_sdr_is_rate_supported(command.parameter)) {
          g_unsupported.fetch_add(1, std::memory_order_relaxed);  // refused, rate unchanged
        } else if (command.parameter != g_rate.load()) {
          stop_stream();  // the driver changes rate only across a restart
          g_rate.store(command.parameter);
          if (!start_stream()) g_errors.fetch_add(1, std::memory_order_relaxed);
        }
        break;
      case static_cast<uint8_t>(Command::set_gain_mode):
        err = esp_rtl_sdr_set_tuner_gain_mode(
            handle, command.parameter ? ESP_RTL_SDR_GAIN_MODE_MANUAL : ESP_RTL_SDR_GAIN_MODE_AUTO);
        break;
      case static_cast<uint8_t>(Command::set_gain):
        err = esp_rtl_sdr_set_tuner_gain(handle, static_cast<int32_t>(command.parameter));
        break;
      case static_cast<uint8_t>(Command::set_freq_correction):
        err = esp_rtl_sdr_set_freq_correction(handle, static_cast<int32_t>(command.parameter));
        break;
      case static_cast<uint8_t>(Command::set_agc_mode):
        err = esp_rtl_sdr_set_rtl_agc(handle, command.parameter != 0);
        break;
      case static_cast<uint8_t>(Command::set_offset_tuning):
        err = esp_rtl_sdr_set_offset_tuning(handle, command.parameter != 0);
        break;
      case static_cast<uint8_t>(Command::set_gain_by_index): {
        int gains[48];
        size_t count = 0;
        if (esp_rtl_sdr_get_tuner_gains(handle, gains, 48, &count) == ESP_OK &&
            command.parameter < count)
          err = esp_rtl_sdr_set_tuner_gain(handle, gains[command.parameter]);
        else
          g_unsupported.fetch_add(1, std::memory_order_relaxed);
        break;
      }
      default:
        // IF gain, test mode, direct sampling, crystal trims and bias-T. The bias-T
        // powers an external device and is never switched by a remote client.
        g_unsupported.fetch_add(1, std::memory_order_relaxed);
        break;
    }
    if (err != ESP_OK) g_errors.fetch_add(1, std::memory_order_relaxed);
  }

  size_t read_iq(uint8_t* destination, size_t max_bytes) override {
    return g_ring.read(destination, max_bytes);
  }
  bool session_alive() override { return g_hooks.usb_ready(); }
  bool stop_requested() override { return !g_enabled.load(std::memory_order_acquire); }
  uint32_t now_ms() override { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
  void sleep_ms(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
  void on_state(ServerState state) override {
    g_state.store(static_cast<uint8_t>(state), std::memory_order_release);
    ESP_LOGI(kTag, "RTL_TCPD_STATE state=%s", rtl_tcp::server_state_name(state));
  }
};

Backend g_backend;

void worker(void*) {
  for (;;) {
    if (!g_enabled.load(std::memory_order_acquire) || !g_hooks.network_up()) {
      g_state.store(static_cast<uint8_t>(ServerState::stopped), std::memory_order_release);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    rtl_tcp::ServerConfig config;
    if (std::strcmp(g_bind, "wifi") == 0) {
      const char* ip = g_hooks.wifi_ip ? g_hooks.wifi_ip() : "";
      if (ip == nullptr || ip[0] == '\0' || std::strcmp(ip, "0.0.0.0") == 0) {  // no address yet: wait, never fall back to all interfaces
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      strlcpy(config.bind_host, ip, sizeof(config.bind_host));
    } else {
      strlcpy(config.bind_host, g_bind, sizeof(config.bind_host));
    }
    config.port = g_port;
    const ServerState result = rtl_tcp::run_server(config, g_backend, g_stats);
    if (result == ServerState::failed) vTaskDelay(pdMS_TO_TICKS(3000));  // bind failed; retry slowly
  }
}

bool valid_bind(const char* text) {
  if (text == nullptr) return false;
  if (std::strcmp(text, "wifi") == 0) return true;
  unsigned a, b, c, d;
  char tail;
  if (std::sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) return false;
  if (a > 255 || b > 255 || c > 255 || d > 255) return false;
  return !(a == 0 && b == 0 && c == 0 && d == 0);  // never "all interfaces"
}

void save() {
  orcsdr::NvsStore store;
  if (!store.begin(kNvsNamespace)) return;
  store.put_bool("enabled", g_enabled.load());
  store.put_string("bind", g_bind);
  store.put_u16("port", g_port);
  store.end();
}

}  // namespace

void begin(const Hooks& hooks) {
  if (g_begun) return;
  g_begun = true;
  g_hooks = hooks;
  orcsdr::NvsStore store;
  if (store.begin(kNvsNamespace, true)) {
    const std::string bind = store.get_string("bind", "");
    if (valid_bind(bind.c_str())) strlcpy(g_bind, bind.c_str(), sizeof(g_bind));
    g_port = store.get_u16("port", rtl_tcp::kDefaultPort);
    if (g_port == 0) g_port = rtl_tcp::kDefaultPort;
    g_enabled.store(store.get_bool("enabled", false) && g_bind[0] != '\0');
    store.end();
  }
  xTaskCreatePinnedToCoreWithCaps(worker, "rtl_tcpd", kTaskStackBytes, nullptr, kTaskPriority,
                                  &g_task, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

bool offer_iq(const uint8_t* data, size_t bytes) {
  if (!g_stream_to_ring.load(std::memory_order_acquire)) return false;
  g_ring.write_block(data, bytes & ~static_cast<size_t>(1));
  return true;
}

bool enabled() { return g_enabled.load(std::memory_order_acquire); }

Status status() {
  Status s;
  s.state = static_cast<ServerState>(g_state.load(std::memory_order_acquire));
  s.enabled = g_enabled.load();
  strlcpy(s.bind, g_bind, sizeof(s.bind));
  s.port = g_port;
  s.client_active = g_client_active.load();
  s.sample_rate_sps = g_rate.load();
  s.frequency_hz = g_frequency.load();
  s.clients_accepted = g_stats.clients_accepted.load();
  s.clients_refused = g_stats.clients_refused.load();
  s.stalled_clients = g_stats.stalled_clients.load();
  s.commands_applied = g_stats.commands_applied.load();
  s.commands_unknown = g_stats.commands_unknown.load();
  s.commands_unsupported = g_unsupported.load();
  s.command_errors = g_errors.load();
  s.bytes_sent = g_stats.bytes_sent.load();
  s.iq_overrun_bytes = g_ring.overrun_bytes();
  return s;
}

bool handle_command(const char* line, char* reply, size_t reply_size) {
  if (line == nullptr || reply == nullptr || reply_size == 0) return false;
  if (std::strncmp(line, "RTL_TCPD", 8) != 0 || (line[8] != '\0' && line[8] != ' ')) return false;
  const char* args = line[8] == ' ' ? line + 9 : "";
  char verb[12]{}, bind[24]{}, port_text[8]{};
  std::sscanf(args, "%11s %23s %7s", verb, bind, port_text);
  if (verb[0] == '\0' || !std::strcmp(verb, "STATUS")) {
    const Status s = status();
    // "wifi" is resolved to the address actually in use, so the owner can see it.
    const char* resolved = (std::strcmp(s.bind, "wifi") == 0 && g_hooks.wifi_ip) ? g_hooks.wifi_ip() : s.bind;
    std::snprintf(reply, reply_size,
                  "RTL_TCPD enabled=%d state=%s bind=%s:%u client=%d rate=%u freq=%u accepted=%u "
                  "refused=%u stalled=%u applied=%u unknown=%u unsupported=%u errors=%u "
                  "sent=%llu iq_overrun=%llu select=%u accept_fail=%u errno=%d",
                  s.enabled ? 1 : 0, rtl_tcp::server_state_name(s.state),
                  resolved && resolved[0] ? resolved : "(none)",
                  s.port, s.client_active ? 1 : 0, static_cast<unsigned>(s.sample_rate_sps),
                  static_cast<unsigned>(s.frequency_hz), static_cast<unsigned>(s.clients_accepted),
                  static_cast<unsigned>(s.clients_refused), static_cast<unsigned>(s.stalled_clients),
                  static_cast<unsigned>(s.commands_applied), static_cast<unsigned>(s.commands_unknown),
                  static_cast<unsigned>(s.commands_unsupported), static_cast<unsigned>(s.command_errors),
                  static_cast<unsigned long long>(s.bytes_sent),
                  static_cast<unsigned long long>(s.iq_overrun_bytes),
                  static_cast<unsigned>(g_stats.select_ready.load()),
                  static_cast<unsigned>(g_stats.accept_failures.load()), g_stats.last_errno.load());
  } else if (!std::strcmp(verb, "ENABLE")) {
    unsigned long port = port_text[0] ? std::strtoul(port_text, nullptr, 10) : rtl_tcp::kDefaultPort;
    if (!valid_bind(bind) || port == 0 || port > 65535) {
      std::snprintf(reply, reply_size,
                    "RTL_TCPD ENABLE error=give an IPv4 address or 'wifi' (never 0.0.0.0) and a port 1-65535");
    } else {
      strlcpy(g_bind, bind, sizeof(g_bind));
      g_port = static_cast<uint16_t>(port);
      g_enabled.store(true, std::memory_order_release);
      save();
      std::snprintf(reply, reply_size,
                    "RTL_TCPD ENABLE bind=%s:%u saved=1 (no authentication: trusted network only)", g_bind,
                    static_cast<unsigned>(g_port));
    }
  } else if (!std::strcmp(verb, "DISABLE")) {
    g_enabled.store(false, std::memory_order_release);
    save();
    std::snprintf(reply, reply_size, "RTL_TCPD DISABLE saved=1");
  } else {
    std::snprintf(reply, reply_size, "RTL_TCPD error=commands are STATUS, ENABLE <ipv4|wifi> [port], DISABLE");
  }
  return true;
}

}  // namespace orcsdr::rtl_tcp_server
