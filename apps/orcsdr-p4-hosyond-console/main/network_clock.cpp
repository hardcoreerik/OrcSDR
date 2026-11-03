#include "network_clock.hpp"
#include "clock_settings.hpp"

#include <atomic>
#include <ctime>
#include <sys/time.h>

#ifdef ORC_NETWORK_CLOCK_TEST
extern "C" uint32_t network_clock_test_monotonic_ms();
extern "C" bool network_clock_test_sntp_enabled();
extern "C" void network_clock_test_sntp_begin(void (*callback)(struct timeval*));
extern "C" void network_clock_test_sntp_end();
extern "C" time_t network_clock_test_wallclock_now();
extern "C" int network_clock_test_set_wallclock(time_t utc);
#endif

#ifndef ORC_NETWORK_CLOCK_TEST
#include <esp_sntp.h>
#include <esp_timer.h>
#endif

namespace network_clock {
namespace {

constexpr uint32_t kTimeoutMs = 20000;
constexpr const char* kServer = "pool.ntp.org";

enum class Engine : uint8_t { idle, syncing };
std::atomic<Engine> g_engine{Engine::idle};
std::atomic<bool> g_request{false};
std::atomic<bool> g_callback_done{false};
std::atomic<bool> g_failed{false};
std::atomic<uint32_t> g_last_success{0};
std::atomic<uint32_t> g_attempts{0};
std::atomic<uint32_t> g_step_revision{0};
std::atomic<uint32_t> g_started_ms{0};
std::atomic<uint32_t> g_last_attempt_ms{0xFFFFFFFFu};
std::atomic<Error> g_error{Error::none};
std::atomic<bool> g_ever_attempted{false};
std::atomic<bool> g_wifi_connected{false};
std::atomic<bool> g_clock_changed{false};
std::atomic<uint8_t> g_set_pending{0};  // 0 idle, 1 producer writing epoch, 2 ready
std::atomic<uint32_t> g_set_epoch{0};

uint32_t monotonic_ms() {
#ifdef ORC_NETWORK_CLOCK_TEST
  return network_clock_test_monotonic_ms();
#else
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
#endif
}

bool sntp_enabled() {
#ifdef ORC_NETWORK_CLOCK_TEST
  return network_clock_test_sntp_enabled();
#else
  return esp_sntp_enabled();
#endif
}

void sntp_begin() {
#ifdef ORC_NETWORK_CLOCK_TEST
  network_clock_test_sntp_begin([](struct timeval*) {
    g_step_revision.fetch_add(1, std::memory_order_acq_rel);
    g_callback_done.store(true, std::memory_order_release);
  });
#else
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, kServer);
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  esp_sntp_set_time_sync_notification_cb([](struct timeval*) {
    g_step_revision.fetch_add(1, std::memory_order_acq_rel);
    g_callback_done.store(true, std::memory_order_release);
  });
  esp_sntp_init();
#endif
}

void sntp_end() {
#ifdef ORC_NETWORK_CLOCK_TEST
  network_clock_test_sntp_end();
#else
  esp_sntp_stop();
#endif
}

time_t wallclock_now() {
#ifdef ORC_NETWORK_CLOCK_TEST
  return network_clock_test_wallclock_now();
#else
  return time(nullptr);
#endif
}

uint32_t plausible_utc(time_t value) {
  if (value < 0) return 0;
  const uint64_t utc = static_cast<uint64_t>(value);
  if (utc < orcsdr::clock_settings::kMinUtc || utc >= orcsdr::clock_settings::kMaxUtc) return 0;
  return static_cast<uint32_t>(utc);
}

bool set_wallclock(time_t value) {
#ifdef ORC_NETWORK_CLOCK_TEST
  return network_clock_test_set_wallclock(value) == 0;
#else
  timeval tv{};
  tv.tv_sec = value;
  return settimeofday(&tv, nullptr) == 0;
#endif
}

void fail(Error error) {
  g_error.store(error, std::memory_order_relaxed);
  g_failed.store(true, std::memory_order_relaxed);
  g_engine.store(Engine::idle, std::memory_order_release);
}

void start_attempt(bool wifi_connected) {
  const uint32_t now = monotonic_ms();
  g_failed.store(false, std::memory_order_relaxed);
  if (!wifi_connected) {
    g_error.store(Error::no_wifi, std::memory_order_relaxed);
    // A disconnected request must not delay the automatic initial sync after reconnect.
    g_ever_attempted.store(false, std::memory_order_relaxed);
    fail(Error::no_wifi);
    return;
  }
  // SNTP is a process-wide singleton. Never stop or reconfigure an owner we do not own.
  if (sntp_enabled()) {
    g_ever_attempted.store(true, std::memory_order_relaxed);
    g_last_attempt_ms.store(now, std::memory_order_relaxed);
    g_attempts.fetch_add(1, std::memory_order_relaxed);
    fail(Error::busy);
    return;
  }
  g_ever_attempted.store(true, std::memory_order_relaxed);
  g_last_attempt_ms.store(now, std::memory_order_relaxed);
  g_attempts.fetch_add(1, std::memory_order_relaxed);
  g_callback_done.store(false, std::memory_order_relaxed);
  g_started_ms.store(now, std::memory_order_relaxed);
  g_error.store(Error::none, std::memory_order_relaxed);
  g_engine.store(Engine::syncing, std::memory_order_release);
  sntp_begin();
}

}  // namespace

bool request_sync() {
  if (g_engine.load(std::memory_order_acquire) == Engine::syncing) return false;
  g_request.store(true, std::memory_order_release);
  return true;
}

bool request_set_utc(uint32_t epoch) {
  if (epoch < orcsdr::clock_settings::kMinUtc || epoch >= orcsdr::clock_settings::kMaxUtc) {
    g_error.store(Error::invalid_time, std::memory_order_relaxed);
    return false;
  }
  uint8_t expected = 0;
  if (!g_set_pending.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return false;
  g_set_epoch.store(epoch, std::memory_order_release);
  g_set_pending.store(2, std::memory_order_release);
  return true;
}

void clock_changed() { g_clock_changed.store(true, std::memory_order_release); }

void service(bool wifi_connected) {
  g_wifi_connected.store(wifi_connected, std::memory_order_relaxed);
  const uint32_t now_ms = monotonic_ms();
  uint8_t set_ready = 2;
  if (g_set_pending.compare_exchange_strong(set_ready, 3, std::memory_order_acq_rel)) {
    const uint32_t requested_epoch = g_set_epoch.load(std::memory_order_acquire);
    g_set_pending.store(0, std::memory_order_release);
    if (g_engine.load(std::memory_order_acquire) == Engine::syncing) sntp_end();
    g_engine.store(Engine::idle, std::memory_order_release);
    g_callback_done.store(false, std::memory_order_release);
    g_request.store(false, std::memory_order_relaxed);
    if (set_wallclock(static_cast<time_t>(requested_epoch))) {
      g_last_success.store(0, std::memory_order_relaxed);
      g_failed.store(false, std::memory_order_relaxed);
      g_error.store(Error::none, std::memory_order_relaxed);
      g_ever_attempted.store(true, std::memory_order_relaxed);
      g_last_attempt_ms.store(now_ms, std::memory_order_relaxed);
      g_step_revision.fetch_add(1, std::memory_order_acq_rel);
    } else {
      g_failed.store(true, std::memory_order_relaxed);
      g_error.store(Error::set_time, std::memory_order_relaxed);
    }
    return;
  }
  if (g_clock_changed.exchange(false, std::memory_order_acq_rel)) {
    if (g_engine.load(std::memory_order_acquire) == Engine::syncing) sntp_end();
    g_engine.store(Engine::idle, std::memory_order_release);
    g_callback_done.store(false, std::memory_order_release);
    g_last_success.store(0, std::memory_order_relaxed);
    g_failed.store(false, std::memory_order_relaxed);
    g_error.store(Error::none, std::memory_order_relaxed);
    g_ever_attempted.store(false, std::memory_order_relaxed);
    g_request.store(false, std::memory_order_relaxed);
    g_step_revision.fetch_add(1, std::memory_order_acq_rel);
  }
  if (g_engine.load(std::memory_order_acquire) == Engine::syncing) {
    if (!wifi_connected) {
      sntp_end();
      fail(Error::no_wifi);
    } else if (g_callback_done.exchange(false, std::memory_order_acq_rel)) {
      const time_t utc = wallclock_now();
      sntp_end();
      const uint32_t valid_utc = plausible_utc(utc);
      if (valid_utc != 0) {
        g_last_success.store(valid_utc, std::memory_order_relaxed);
        g_failed.store(false, std::memory_order_relaxed);
        g_error.store(Error::none, std::memory_order_relaxed);
        g_engine.store(Engine::idle, std::memory_order_release);
      } else {
        fail(Error::invalid_time);
      }
    } else if (static_cast<uint32_t>(now_ms - g_started_ms.load(std::memory_order_relaxed)) >= kTimeoutMs) {
      sntp_end();
      fail(Error::timeout);
    }
  }

  if (g_engine.load(std::memory_order_acquire) == Engine::syncing) return;
  const bool explicit_request = g_request.exchange(false, std::memory_order_acq_rel);
  const uint32_t last_try = g_last_attempt_ms.load(std::memory_order_relaxed);
  const uint32_t since_attempt = g_ever_attempted.load(std::memory_order_relaxed)
      ? static_cast<uint32_t>(now_ms - last_try) : 0xFFFFFFFFu;
  const uint32_t utc = plausible_utc(wallclock_now());
  orcsdr::clock_sync::Input input{};
  input.clock_valid = utc != 0;
  input.wifi_connected = wifi_connected;
  input.last_attempt_failed = g_failed.load(std::memory_order_relaxed);
  input.now_utc = utc;
  input.last_sync_utc = g_last_success.load(std::memory_order_relaxed);
  orcsdr::clock_sync::DueInput due{};
  due.status = input;
  // The P4 network clock is Settings functionality; FT8-specific cadence is a future caller concern.
  // An initial sync/retry is eligible whenever Wi-Fi is available.
  due.decoder_running = true;
  due.ms_since_last_attempt = since_attempt;
  if (explicit_request) {
    if (wifi_connected) {
      start_attempt(true);
    } else {
      g_request.store(true, std::memory_order_release);  // keep the explicit request queued until reconnect
      fail(Error::no_wifi);
    }
  } else if (orcsdr::clock_sync::auto_sync_due(due)) {
    start_attempt(wifi_connected);
  }
}

Status status() {
  Status out{};
  const uint32_t utc = plausible_utc(wallclock_now());
  out.wallclock_valid = utc != 0;
  out.last_success_utc = g_last_success.load(std::memory_order_relaxed);
  out.syncing = g_engine.load(std::memory_order_acquire) == Engine::syncing;
  out.failed = g_failed.load(std::memory_order_relaxed);
  out.last_error = g_error.load(std::memory_order_relaxed);
  out.attempts = g_attempts.load(std::memory_order_relaxed);
  out.time_step_revision = g_step_revision.load(std::memory_order_relaxed);
  orcsdr::clock_sync::Input input{};
  input.clock_valid = out.wallclock_valid;
  input.wifi_connected = g_wifi_connected.load(std::memory_order_relaxed);
  input.syncing = out.syncing;
  input.last_attempt_failed = out.failed;
  input.now_utc = utc;
  input.last_sync_utc = out.last_success_utc;
  out.level = orcsdr::clock_sync::evaluate(input);
  out.freshness_age_s = orcsdr::clock_sync::age_seconds(input);
  return out;
}

}  // namespace network_clock
