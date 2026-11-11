#include "rtl_tcp_source.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
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
    // A stop request is not a cause: when a start gives up after its wait, the reason worth showing is the one that made it fail (busy, refused, ...).
    if (reason != EndReason::none && reason != EndReason::stopped) g_reason.store(static_cast<uint8_t>(reason), std::memory_order_release);
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

// Receive-only network benchmark: connects to the configured server and just counts bytes, so the
// link (Wi-Fi + SDIO) is measured without the demodulator or the IQ ring in the path.
std::atomic<bool> g_bench_running{false};
TaskHandle_t g_bench_task = nullptr;   // parked at its end, deleted by reap() (never self-deleted; see bench_task)

void bench_task(void* arg) {
  const uint32_t seconds = reinterpret_cast<uintptr_t>(arg);
  uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(65536, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* found = nullptr;
  char port_text[8];
  std::snprintf(port_text, sizeof(port_text), "%u", static_cast<unsigned>(g_config.port));
  int fd = -1;
  if (buffer != nullptr && getaddrinfo(g_config.host, port_text, &hints, &found) == 0 && found != nullptr) {
    fd = socket(found->ai_family, found->ai_socktype, 0);
    if (fd >= 0 && connect(fd, found->ai_addr, found->ai_addrlen) != 0) {
      close(fd);
      fd = -1;
    }
  }
  if (found != nullptr) freeaddrinfo(found);
  if (fd < 0) {
    ESP_LOGW("rtl_tcp", "RTL_TCP_BENCH error=connect_failed");
  } else {
    timeval timeout{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    const int64_t start = esp_timer_get_time();
    int64_t mark = start;
    uint64_t total = 0, marked = 0;
    while (esp_timer_get_time() - start < static_cast<int64_t>(seconds) * 1000000) {
      const int n = recv(fd, buffer, 65536, 0);
      if (n > 0) total += static_cast<uint64_t>(n);
      const int64_t now = esp_timer_get_time();
      if (now - mark >= 2000000) {
        ESP_LOGI("rtl_tcp", "RTL_TCP_BENCH t=%us MB/s=%.2f", static_cast<unsigned>((now - start) / 1000000),
                 static_cast<double>(total - marked) / (static_cast<double>(now - mark) / 1e6) / 1e6);
        mark = now;
        marked = total;
      }
    }
    const double elapsed = static_cast<double>(esp_timer_get_time() - start) / 1e6;
    ESP_LOGI("rtl_tcp", "RTL_TCP_BENCH done bytes=%llu seconds=%.1f MB/s=%.2f Mbit/s=%.1f",
             static_cast<unsigned long long>(total), elapsed, static_cast<double>(total) / elapsed / 1e6,
             static_cast<double>(total) * 8.0 / elapsed / 1e6);
    close(fd);
  }
  heap_caps_free(buffer);
  g_bench_running.store(false, std::memory_order_release);
  // A self-deleting WithCaps task needs a temporary cleanup task from internal RAM and abort()s when that fails (ESP-IDF 5.5.4).
  // Park instead; reap() deletes this task by handle from the application loop.
  vTaskSuspend(nullptr);
}

// UDP variant: binds the given port and counts datagrams and sequence gaps from a sender that puts a
// 32-bit counter in the first four payload bytes. Separates radio+bus capacity from TCP behaviour.
void bench_udp_task(void* arg) {
  const uint32_t packed = reinterpret_cast<uintptr_t>(arg);
  const uint32_t seconds = packed & 0xFFFF;
  const uint16_t port = static_cast<uint16_t>(packed >> 16);
  uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  bool ok = buffer != nullptr && fd >= 0;
  if (ok) {
    int big = 262144;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &big, sizeof(big));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    ok = bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    timeval timeout{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  }
  if (!ok) {
    ESP_LOGW("rtl_tcp", "RTL_UDP_BENCH error=bind_failed");
  } else {
    const int64_t start = esp_timer_get_time();
    uint64_t bytes = 0, packets = 0, lost = 0;
    int64_t first_seen = 0;
    uint32_t expected = 0;
    bool have_expected = false;
    while (esp_timer_get_time() - start < static_cast<int64_t>(seconds) * 1000000 + 2000000) {
      const int n = recv(fd, buffer, 2048, 0);
      if (n >= 4) {
        if (first_seen == 0) first_seen = esp_timer_get_time();
        uint32_t seq;
        std::memcpy(&seq, buffer, 4);
        if (have_expected && seq > expected) lost += seq - expected;
        expected = seq + 1;
        have_expected = true;
        bytes += static_cast<uint64_t>(n);
        ++packets;
        if (esp_timer_get_time() - first_seen >= static_cast<int64_t>(seconds) * 1000000) break;
      }
    }
    const double elapsed = first_seen ? static_cast<double>(esp_timer_get_time() - first_seen) / 1e6 : 0.0;
    ESP_LOGI("rtl_tcp", "RTL_UDP_BENCH done packets=%llu lost=%llu bytes=%llu seconds=%.1f MB/s=%.2f Mbit/s=%.1f",
             static_cast<unsigned long long>(packets), static_cast<unsigned long long>(lost),
             static_cast<unsigned long long>(bytes), elapsed,
             elapsed > 0 ? static_cast<double>(bytes) / elapsed / 1e6 : 0.0,
             elapsed > 0 ? static_cast<double>(bytes) * 8.0 / elapsed / 1e6 : 0.0);
  }
  if (fd >= 0) close(fd);
  heap_caps_free(buffer);
  g_bench_running.store(false, std::memory_order_release);
  // A self-deleting WithCaps task needs a temporary cleanup task from internal RAM and abort()s when that fails (ESP-IDF 5.5.4).
  // Park instead; reap() deletes this task by handle from the application loop.
  vTaskSuspend(nullptr);
}



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

uint8_t g_rate_cap = 0;   // see Tuning::rate_cap
std::atomic<bool> g_wifi_keepalive{true};   // on by default; it only acts while a remote receiver is selected

// The sample rate a stream may ask for under the user's cap. The adapter ladder works on whatever it is given, so the cap is applied first.
uint32_t capped(uint32_t wanted_sps) {
  const uint32_t limit = g_rate_cap == 1 ? 960000u : g_rate_cap == 2 ? 240000u : 0u;
  return (limit != 0 && wanted_sps > limit) ? limit : wanted_sps;
}

void save_settings() {
  NvsStore store;
  if (!store.begin(kNvsNamespace)) return;
  store.put_u8("mode", static_cast<uint8_t>(g_mode));
  store.put_string("host", g_config.host);
  store.put_u16("port", g_config.port);
  store.put_u8("cap", g_rate_cap);
  store.put_u8("wifikeep", g_wifi_keepalive.load() ? 1 : 0);
  store.put_u8("ppm_set", g_desired.ppm_set ? 1 : 0);
  store.put_i32("ppm", g_desired.ppm);
  store.put_u8("dir_set", g_desired.direct_set ? 1 : 0);
  store.put_u8("dir", g_desired.direct_mode);
  store.put_u8("off_set", g_desired.offset_set ? 1 : 0);
  store.put_u8("off", g_desired.offset_on ? 1 : 0);
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
    g_rate_cap = store.get_u8("cap", 0);
    if (g_rate_cap > 2) g_rate_cap = 0;
    g_wifi_keepalive.store(store.get_u8("wifikeep", 1) != 0);
    g_desired.ppm_set = store.get_u8("ppm_set", 0) != 0;
    g_desired.ppm = store.get_i32("ppm", 0);
    if (g_desired.ppm < -1000 || g_desired.ppm > 1000) g_desired.ppm_set = false, g_desired.ppm = 0;
    g_desired.direct_set = store.get_u8("dir_set", 0) != 0;
    g_desired.direct_mode = store.get_u8("dir", 0);
    if (g_desired.direct_mode > 2) g_desired.direct_set = false, g_desired.direct_mode = 0;
    g_desired.offset_set = store.get_u8("off_set", 0) != 0;
    g_desired.offset_on = store.get_u8("off", 0) != 0;
    store.end();
  }
  reset_gain_to_auto();   // never rely on what the server's tuner was left at: send a known gain state on the first connect
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

uint32_t adapted_rate(uint32_t wanted_sps) { return g_rate_adapter.rate(capped(wanted_sps)); }

rtl_tcp::RateAdapter::Change note_link_and_adapt(uint32_t link_pct, uint32_t wanted_sps, uint32_t floor_sps) {
  return g_rate_adapter.note(link_pct, capped(wanted_sps), floor_sps);
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
  // A server that accepts the connection but sends no greeting is holding its one slot for another client. That is worth waiting for:
  // the worker keeps retrying and connects as soon as the slot frees, so the wait stretches (to kBusyWaitMs) only in that case and
  // still ends at once on a cancel. Any other failure keeps the caller's own timeout.
  constexpr uint32_t kBusyWaitMs = 120000;
  uint32_t limit = timeout_ms;
  for (uint32_t waited = 0; waited < limit; waited += 50) {
    const State s = state();
    if (s == State::streaming) return true;
    if (s == State::failed || !running()) break;
    if (cancelled && cancelled()) break;
    if (waited + 50 >= limit && limit < kBusyWaitMs &&
        static_cast<EndReason>(g_reason.load(std::memory_order_acquire)) == EndReason::no_greeting)
      limit = kBusyWaitMs;
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

void reset_gain_to_auto() {
  // rtl_tcp keeps the tuner's gain mode, gain and digital AGC between clients, and the Tab5 only sent gain commands after
  // someone had set one in the session. A new stream therefore inherited whatever an earlier dashboard or client had left
  // (seen: FT8 over a remote stream started at a near-zero manual gain, about 28 dB too low, while the screen said AUTO).
  // The state sent on every connect is now defined: tuner AGC on, digital AGC off, until a control changes it.
  Lock lock;
  g_desired.gain_mode_set = true;
  g_desired.gain_manual = false;
  g_desired.gain_set = false;
  g_desired.gain_tenths_db = 0;
  g_desired.agc_set = true;
  g_desired.agc_on = false;
}

bool set_gain_manual(bool manual) {
  {
    Lock lock;
    g_desired.gain_mode_set = true;
    g_desired.gain_manual = manual;
    if (!manual) g_desired.gain_set = false;  // a stale manual value must not be replayed after AUTO
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

Tuning tuning() {
  Tuning t;
  Lock lock;
  t.rate_cap = g_rate_cap;
  t.ppm_set = g_desired.ppm_set;
  t.ppm = g_desired.ppm;
  t.direct_set = g_desired.direct_set;
  t.direct_mode = g_desired.direct_mode;
  t.offset_set = g_desired.offset_set;
  t.offset_on = g_desired.offset_on;
  return t;
}

const char* rate_cap_name(uint8_t cap) {
  return cap == 1 ? "MAX 0.96 MS/s" : cap == 2 ? "0.24 MS/s ONLY" : "AUTO (UP TO 1.44)";
}

bool set_rate_cap(uint8_t cap) {
  if (cap > 2) return false;
  g_rate_cap = cap;
  g_rate_adapter.reset();
  { Lock lock; save_settings(); }
  return true;
}

bool set_ppm(int32_t ppm) {
  if (ppm < -1000 || ppm > 1000) return false;
  {
    Lock lock;
    g_desired.ppm_set = true;
    g_desired.ppm = ppm;
    save_settings();
  }
  return !running() || queue_command(Command::set_freq_correction, rtl_tcp::parameter_signed(ppm));
}

bool set_direct_sampling(uint8_t mode) {
  if (mode > 2) return false;
  {
    Lock lock;
    g_desired.direct_set = true;
    g_desired.direct_mode = mode;
    save_settings();
  }
  return !running() || queue_command(Command::set_direct_sampling, mode);
}

bool set_offset_tuning(bool on) {
  {
    Lock lock;
    g_desired.offset_set = true;
    g_desired.offset_on = on;
    save_settings();
  }
  return !running() || queue_command(Command::set_offset_tuning, on ? 1u : 0u);
}

bool wifi_keepalive() { return g_wifi_keepalive.load(); }

void set_wifi_keepalive(bool on) {
  g_wifi_keepalive.store(on);
  Lock lock;
  save_settings();
}

RemoteGain remote_gain() {
  RemoteGain r;
  {
    Lock lock;
    r.mode_known = g_desired.gain_mode_set;
    r.manual = g_desired.gain_manual;
    r.gain_known = g_desired.gain_set;
    r.gain_tenths_db = g_desired.gain_tenths_db;
    r.agc_known = g_desired.agc_set;
    r.agc_on = g_desired.agc_on;
  }
  r.tuner_type_raw = g_stats.tuner_type_raw.load(std::memory_order_relaxed);
  r.gain_count = g_stats.gain_count.load(std::memory_order_relaxed);
  return r;
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

void reap() {
  if (g_bench_task != nullptr && !g_bench_running.load(std::memory_order_acquire)) {
    vTaskDeleteWithCaps(g_bench_task);   // already parked; frees its PSRAM stack and control block without a cleanup task
    g_bench_task = nullptr;
  }
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
  } else if (!std::strcmp(verb, "BENCHUDP")) {
    const unsigned long seconds = arg1[0] ? std::strtoul(arg1, nullptr, 10) : 10;
    const unsigned long port_number = arg2[0] ? std::strtoul(arg2, nullptr, 10) : 1236;
    if (g_running.load(std::memory_order_acquire) || seconds == 0 || seconds > 120 || port_number == 0 ||
        port_number > 65535) {
      std::snprintf(reply, reply_size, "RTL_TCP BENCHUDP error=needs 1-120 s, a port and no active stream");
    } else if (reap(), g_bench_running.exchange(true)) {
      std::snprintf(reply, reply_size, "RTL_TCP BENCHUDP error=already running");
    } else if (xTaskCreatePinnedToCoreWithCaps(bench_udp_task, "rtl_bench", 6144,
                                               reinterpret_cast<void*>((port_number << 16) | seconds), 10, &g_bench_task, 0,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
      g_bench_running.store(false, std::memory_order_release);   // otherwise a failed start would block every later bench
      std::snprintf(reply, reply_size, "RTL_TCP BENCHUDP error=could not start the task");
    } else {
      std::snprintf(reply, reply_size, "RTL_TCP BENCHUDP listening port=%lu seconds=%lu", port_number, seconds);
    }
  } else if (!std::strcmp(verb, "BENCH")) {
    const unsigned long seconds = arg1[0] ? std::strtoul(arg1, nullptr, 10) : 10;
    if (g_running.load(std::memory_order_acquire) || seconds == 0 || seconds > 120 || !configured()) {
      std::snprintf(reply, reply_size, "RTL_TCP BENCH error=needs a configured server, 1-120 s and no active stream");
    } else if (reap(), g_bench_running.exchange(true)) {
      std::snprintf(reply, reply_size, "RTL_TCP BENCH error=already running");
    } else if (xTaskCreatePinnedToCoreWithCaps(bench_task, "rtl_bench", 6144, reinterpret_cast<void*>(seconds), 10, &g_bench_task, 0,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
      g_bench_running.store(false, std::memory_order_release);
      std::snprintf(reply, reply_size, "RTL_TCP BENCH error=could not start the task");
    } else {
      std::snprintf(reply, reply_size, "RTL_TCP BENCH started seconds=%lu (watch RTL_TCP_BENCH lines)", seconds);
    }
  } else if (!std::strcmp(verb, "HOST")) {
    unsigned long port_number = arg2[0] ? std::strtoul(arg2, nullptr, 10) : rtl_tcp::kDefaultPort;
    if (port_number == 0 || port_number > 65535 || !set_endpoint(arg1, static_cast<uint16_t>(port_number)))
      std::snprintf(reply, reply_size,
                    "RTL_TCP HOST error=need an IPv4 address or host name, a port 1-65535, and no active stream");
    else
      std::snprintf(reply, reply_size, "RTL_TCP HOST server=%s:%u saved=1", host(), port());
  } else if (!std::strcmp(verb, "RATECAP")) {
    if (arg1[0] && set_rate_cap(static_cast<uint8_t>(std::strtoul(arg1, nullptr, 10))))
      std::snprintf(reply, reply_size, "RTL_TCP RATECAP cap=%u (%s) saved=1", g_rate_cap, rate_cap_name(g_rate_cap));
    else
      std::snprintf(reply, reply_size, "RTL_TCP RATECAP error=use 0 (auto), 1 (max 0.96 MS/s) or 2 (0.24 MS/s)");
  } else if (!std::strcmp(verb, "WIFIKEEP")) {
    if (arg1[0] == '0' || arg1[0] == '1') {
      set_wifi_keepalive(arg1[0] == '1');
      std::snprintf(reply, reply_size, "RTL_TCP WIFIKEEP on=%d saved=1", wifi_keepalive() ? 1 : 0);
    } else {
      std::snprintf(reply, reply_size, "RTL_TCP WIFIKEEP on=%d (use 0 or 1)", wifi_keepalive() ? 1 : 0);
    }
  } else if (!std::strcmp(verb, "PPM")) {
    const long value = std::strtol(arg1, nullptr, 10);
    if (arg1[0] && set_ppm(static_cast<int32_t>(value))) std::snprintf(reply, reply_size, "RTL_TCP PPM ppm=%ld saved=1", value);
    else std::snprintf(reply, reply_size, "RTL_TCP PPM error=use -1000..1000");
  } else if (!std::strcmp(verb, "DIRECT")) {
    const unsigned long value = std::strtoul(arg1, nullptr, 10);
    if (arg1[0] && set_direct_sampling(static_cast<uint8_t>(value))) std::snprintf(reply, reply_size, "RTL_TCP DIRECT mode=%lu saved=1", value);
    else std::snprintf(reply, reply_size, "RTL_TCP DIRECT error=use 0 (off), 1 (I) or 2 (Q)");
  } else if (!std::strcmp(verb, "OFFSET")) {
    if (arg1[0] && set_offset_tuning(std::strtoul(arg1, nullptr, 10) != 0)) std::snprintf(reply, reply_size, "RTL_TCP OFFSET on=%s saved=1", std::strtoul(arg1, nullptr, 10) ? "1" : "0");
    else std::snprintf(reply, reply_size, "RTL_TCP OFFSET error=use 0 or 1");
  } else {
    std::snprintf(reply, reply_size, "RTL_TCP error=commands are STATUS, SOURCE auto|usb|remote, HOST <ip|name> [port], RATECAP 0-2, PPM n, DIRECT 0-2, OFFSET 0|1");
  }
  return true;
}

}  // namespace orcsdr::rtl_tcp_source
