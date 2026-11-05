#include "rtl_tcp_source.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "nvs_store.hpp"

namespace orcsdr::rtl_tcp_source {
namespace {

using rtl_tcp::Command;
using rtl_tcp::EndReason;
using rtl_tcp::State;

constexpr size_t kCommandQueueDepth = 16;
// Below the DSP task (6) and the receiver app task (5); on core 0, which the
// USB host leaves free when no dongle is streaming.
constexpr UBaseType_t kTaskPriority = 4;
constexpr uint32_t kTaskStackBytes = 8192;
constexpr uint32_t kStopWaitMs = 3000;
constexpr const char* kNvsNamespace = "rtltcp";

IqSink g_sink{};
source::Mode g_mode = source::Mode::automatic;
rtl_tcp::SessionConfig g_config{};
rtl_tcp::Desired g_desired{};
rtl_tcp::Stats g_stats;
SemaphoreHandle_t g_lock = nullptr;
TaskHandle_t g_task = nullptr;
uint8_t g_commands[kCommandQueueDepth][rtl_tcp::kCommandBytes];
size_t g_command_count = 0;
std::atomic<bool> g_running{false};
std::atomic<bool> g_stop{false};
std::atomic<uint8_t> g_state{static_cast<uint8_t>(State::idle)};
std::atomic<uint8_t> g_reason{static_cast<uint8_t>(EndReason::none)};
std::atomic<bool> g_begun{false};
rtl_tcp::RateAdapter g_rate_adapter;  // touched only by the receiver task and the serial/UI setters

struct Lock {
  Lock() { if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY); }
  ~Lock() { if (g_lock) xSemaphoreGive(g_lock); }
};

class AppEnv final : public rtl_tcp::Env {
 public:
  bool acquire(uint8_t** slot, size_t* capacity) override {
    return g_sink.acquire && g_sink.acquire(slot, capacity);
  }
  void submit(uint8_t* slot, size_t bytes) override { g_sink.submit(slot, bytes); }
  void release(uint8_t* slot) override { g_sink.release(slot); }
  void discontinuity() override {
    if (g_sink.discontinuity) g_sink.discontinuity();
  }
  bool pop_command(uint8_t out[rtl_tcp::kCommandBytes]) override {
    Lock lock;
    if (g_command_count == 0) return false;
    std::memcpy(out, g_commands[0], rtl_tcp::kCommandBytes);
    std::memmove(g_commands[0], g_commands[1],
                 (g_command_count - 1) * rtl_tcp::kCommandBytes);
    --g_command_count;
    return true;
  }
  rtl_tcp::Desired desired() override {
    Lock lock;
    return g_desired;
  }
  bool stop_requested() override { return g_stop.load(std::memory_order_acquire); }
  void on_state(State state, EndReason reason) override {
    g_state.store(static_cast<uint8_t>(state), std::memory_order_release);
    if (reason != EndReason::none) g_reason.store(static_cast<uint8_t>(reason), std::memory_order_release);
    // State changes are rare (connect, stream, reconnect), so this is cheap to keep.
    ESP_LOGI("rtl_tcp", "RTL_TCP_STATE state=%s reason=%s errno=%d bytes=%llu",
             rtl_tcp::state_name(state), rtl_tcp::end_reason_name(reason),
             g_stats.last_errno.load(std::memory_order_relaxed),
             static_cast<unsigned long long>(g_stats.bytes_received.load(std::memory_order_relaxed)));
  }
  uint32_t now_ms() override { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
  void sleep_ms(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
};

AppEnv g_env;

void worker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    rtl_tcp::SessionConfig config;
    {
      Lock lock;
      config = g_config;
    }
    rtl_tcp::run(config, g_env, g_stats);
    g_running.store(false, std::memory_order_release);
  }
}

bool queue_command(Command command, uint32_t parameter) {
  Lock lock;
  if (g_command_count >= kCommandQueueDepth) return false;  // never grow without bound
  rtl_tcp::encode_command(command, parameter, g_commands[g_command_count++]);
  return true;
}

void save_settings() {
  NvsStore store;
  if (!store.begin(kNvsNamespace)) return;
  store.put_u8("mode", static_cast<uint8_t>(g_mode));
  store.put_string("host", g_config.host);
  store.put_u16("port", g_config.port);
  store.end();
}

}  // namespace

void begin(const IqSink& sink) {
  if (g_begun.exchange(true)) return;
  g_sink = sink;
  g_lock = xSemaphoreCreateMutex();
  NvsStore store;
  if (store.begin(kNvsNamespace, true)) {
    const uint8_t mode_value = store.get_u8("mode", 0);
    g_mode = mode_value <= static_cast<uint8_t>(source::Mode::remote_tcp)
                 ? static_cast<source::Mode>(mode_value)
                 : source::Mode::automatic;
    const std::string saved = store.get_string("host", "");
    if (rtl_tcp::valid_host(saved.c_str())) strlcpy(g_config.host, saved.c_str(), sizeof(g_config.host));
    g_config.port = store.get_u16("port", rtl_tcp::kDefaultPort);
    if (g_config.port == 0) g_config.port = rtl_tcp::kDefaultPort;
    store.end();
  }
  xTaskCreatePinnedToCoreWithCaps(worker, "rtl_tcp", kTaskStackBytes, nullptr, kTaskPriority,
                                  &g_task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

source::Mode mode() { return g_mode; }

bool set_mode(source::Mode value) {
  g_mode = value;
  g_rate_adapter.reset();
  save_settings();
  return true;
}

uint32_t adapted_rate(uint32_t wanted_sps) { return g_rate_adapter.rate(wanted_sps); }

bool note_link_and_maybe_downshift(uint32_t link_pct, uint32_t wanted_sps, uint32_t floor_sps) {
  return g_rate_adapter.note(link_pct, wanted_sps, floor_sps);
}

bool configured() { return rtl_tcp::valid_host(g_config.host) && g_config.port != 0; }
const char* host() { return g_config.host; }
uint16_t port() { return g_config.port; }

bool set_endpoint(const char* host_name, uint16_t port_number) {
  if (!rtl_tcp::valid_host(host_name) || port_number == 0) return false;
  if (g_running.load(std::memory_order_acquire)) return false;  // never retarget a live stream
  {
    Lock lock;
    strlcpy(g_config.host, host_name, sizeof(g_config.host));
    g_config.port = port_number;
  }
  g_rate_adapter.reset();
  save_settings();
  return true;
}

bool start(uint32_t sample_rate_sps, uint32_t frequency_hz) {
  if (!g_begun.load() || g_task == nullptr || !configured()) return false;
  if (g_running.exchange(true, std::memory_order_acq_rel)) return false;
  {
    Lock lock;
    g_desired.sample_rate_sps = sample_rate_sps;
    g_desired.frequency_hz = frequency_hz;
    g_command_count = 0;  // a new session starts from the desired state, not old commands
  }
  g_stop.store(false, std::memory_order_release);
  g_state.store(static_cast<uint8_t>(State::connecting), std::memory_order_release);
  g_reason.store(static_cast<uint8_t>(EndReason::none), std::memory_order_release);
  xTaskNotifyGive(g_task);
  return true;
}

bool start_and_wait(uint32_t sample_rate_sps, uint32_t frequency_hz, uint32_t timeout_ms,
                    bool (*cancelled)()) {
  if (!start(sample_rate_sps, frequency_hz)) return false;
  for (uint32_t waited = 0; waited < timeout_ms; waited += 50) {
    const State s = state();
    if (s == State::streaming) return true;
    if (s == State::failed || !running()) break;
    if (cancelled && cancelled()) break;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  stop();
  return false;
}

bool alive() { return running() && state() != State::failed; }

void stop() {
  if (!g_running.load(std::memory_order_acquire)) return;
  g_stop.store(true, std::memory_order_release);
  for (uint32_t waited = 0; waited < kStopWaitMs && g_running.load(std::memory_order_acquire);
       waited += 10)
    vTaskDelay(pdMS_TO_TICKS(10));
}

bool running() { return g_running.load(std::memory_order_acquire); }
State state() { return static_cast<State>(g_state.load(std::memory_order_acquire)); }
bool streaming() { return state() == State::streaming; }

bool retune(uint32_t frequency_hz) {
  {
    Lock lock;
    g_desired.frequency_hz = frequency_hz;  // re-applied after a reconnect
  }
  return queue_command(Command::set_frequency, frequency_hz);
}

bool set_gain_manual(bool manual) {
  {
    Lock lock;
    g_desired.gain_mode_set = true;
    g_desired.gain_manual = manual;
  }
  return queue_command(Command::set_gain_mode, manual ? 1u : 0u);
}

bool set_gain_tenths_db(int32_t tenths_db) {
  {
    Lock lock;
    g_desired.gain_set = true;
    g_desired.gain_tenths_db = tenths_db;
  }
  return queue_command(Command::set_gain, rtl_tcp::parameter_signed(tenths_db));
}

bool set_agc(bool on) {
  {
    Lock lock;
    g_desired.agc_set = true;
    g_desired.agc_on = on;
  }
  return queue_command(Command::set_agc_mode, on ? 1u : 0u);
}

uint64_t bytes_total() { return g_stats.bytes_received.load(std::memory_order_relaxed); }
uint32_t link_ratio_pct() { return g_stats.link_ratio_pct.load(std::memory_order_relaxed); }

Status status() {
  Status s;
  s.state = state();
  s.last_reason = static_cast<EndReason>(g_reason.load(std::memory_order_acquire));
  {
    Lock lock;
    strlcpy(s.host, g_config.host, sizeof(s.host));
    s.port = g_config.port;
    s.frequency_hz = g_desired.frequency_hz;
    s.sample_rate_sps = g_desired.sample_rate_sps;
  }
  s.tuner_type_raw = g_stats.tuner_type_raw.load(std::memory_order_relaxed);
  s.gain_count = g_stats.gain_count.load(std::memory_order_relaxed);
  s.bytes_received = g_stats.bytes_received.load(std::memory_order_relaxed);
  s.bytes_dropped = g_stats.bytes_dropped.load(std::memory_order_relaxed);
  s.connections = g_stats.connections.load(std::memory_order_relaxed);
  s.reconnects = g_stats.reconnects.load(std::memory_order_relaxed);
  s.last_errno = g_stats.last_errno.load(std::memory_order_relaxed);
  s.link_ratio_pct = g_stats.link_ratio_pct.load(std::memory_order_relaxed);
  return s;
}

bool handle_command(const char* line, char* reply, size_t reply_size) {
  if (line == nullptr || reply == nullptr || reply_size == 0) return false;
  if (std::strncmp(line, "RTL_TCP", 7) != 0 || (line[7] != '\0' && line[7] != ' ')) return false;
  const char* args = line[7] == ' ' ? line + 8 : "";
  while (*args == ' ') ++args;
  char verb[16]{};
  char arg1[80]{};
  char arg2[16]{};
  std::sscanf(args, "%15s %79s %15s", verb, arg1, arg2);
  if (verb[0] == '\0' || !std::strcmp(verb, "STATUS")) {
    const Status s = status();
    std::snprintf(reply, reply_size,
                  "RTL_TCP mode=%s server=%s:%u state=%s last=\"%s\" tuner=%s gains=%u "
                  "bytes=%llu dropped=%llu connections=%u reconnects=%u errno=%d link=%u%%",
                  source::mode_name(mode()), s.host[0] ? s.host : "(none)", s.port,
                  rtl_tcp::state_name(s.state), rtl_tcp::end_reason_name(s.last_reason),
                  rtl_tcp::tuner_type_name(s.tuner_type_raw), static_cast<unsigned>(s.gain_count),
                  static_cast<unsigned long long>(s.bytes_received),
                  static_cast<unsigned long long>(s.bytes_dropped), static_cast<unsigned>(s.connections),
                  static_cast<unsigned>(s.reconnects), s.last_errno,
                  static_cast<unsigned>(s.link_ratio_pct));
  } else if (!std::strcmp(verb, "SOURCE")) {
    source::Mode requested;
    if (!source::parse_mode(arg1, &requested)) {
      std::snprintf(reply, reply_size, "RTL_TCP SOURCE error=use auto, usb or remote");
    } else {
      set_mode(requested);
      std::snprintf(reply, reply_size, "RTL_TCP SOURCE mode=%s saved=1 (takes effect on the next start)",
                    source::mode_name(requested));
    }
  } else if (!std::strcmp(verb, "HOST")) {
    unsigned long port_number = arg2[0] ? std::strtoul(arg2, nullptr, 10) : rtl_tcp::kDefaultPort;
    if (port_number == 0 || port_number > 65535 || !set_endpoint(arg1, static_cast<uint16_t>(port_number)))
      std::snprintf(reply, reply_size,
                    "RTL_TCP HOST error=need an IPv4 address or host name, a port 1-65535, and no active stream");
    else
      std::snprintf(reply, reply_size, "RTL_TCP HOST server=%s:%u saved=1", host(), port());
  } else {
    std::snprintf(reply, reply_size, "RTL_TCP error=commands are STATUS, SOURCE auto|usb|remote, HOST <ip|name> [port]");
  }
  return true;
}

}  // namespace orcsdr::rtl_tcp_source
