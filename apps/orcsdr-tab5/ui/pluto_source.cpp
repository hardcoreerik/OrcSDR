// PlutoSDR local receiver source (see pluto_source.hpp). The device logic is in the esp-pluto-sdr driver; this file watches for the device,
// starts and stops its stream on the application's request, and copies the driver's blocks into the application's IQ ring slots.
#include "pluto_source.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pluto_sdr.h"

namespace orcsdr::pluto_source {
namespace {

const char* TAG = "PLUTO";
constexpr int kQuietRecoveryUs = 3000000;  // a stream quiet for longer than this is reported dead so the application can restart it

pluto_sdr_handle_t g_h = nullptr;
IqSink g_sink{};
Events g_events{};
uint8_t* g_stage = nullptr;
size_t g_stage_cap = 0;

std::atomic<bool> g_present{false};
std::atomic<bool> g_running{false};      // a stream was requested and has not been stopped
std::atomic<bool> g_alive{false};        // the reader sees the stream delivering (or recently did)
std::atomic<bool> g_first_block{false};
std::atomic<bool> g_reader_run{false};
std::atomic<bool> g_reader_exited{true};
std::atomic<uint32_t> g_ring_drops{0};

// Recovery ladder. A Pluto's USB gadget can stay wedged after the host resets, and nothing short of removing its 5 V brings it back. The IDF host
// library has no per-device reset, so the ladder is: let the driver retry on its own, then cycle the port's 5 V rail through the application's
// hook, backing off between tries, and finally report that the board needs a manual power cycle.
constexpr int64_t kFailedStartGraceUs = 30000000;   // one failed start with no success for this long counts as wedged
constexpr int64_t kLostGraceUs = 30000000;          // a Pluto seen before and absent this long counts as wedged
constexpr int64_t kBackoffUs[] = {45000000, 120000000, 300000000, 600000000};
constexpr uint32_t kMaxCycles = 6;
std::atomic<uint32_t> g_start_failures{0};
std::atomic<int64_t> g_last_failure_us{0};
std::atomic<uint32_t> g_cycles{0};
std::atomic<bool> g_gave_up{false};

uint32_t clamp32(uint64_t v) { return v > 0xFFFFFFFFULL ? 0xFFFFFFFFu : static_cast<uint32_t>(v); }

// Copies one driver block into as many ring slots as it needs. A block the ring cannot take is dropped and counted: the stream never waits
// on the demodulators.
void deliver(const uint8_t* data, size_t bytes) {
  while (bytes > 0) {
    uint8_t* slot = nullptr;
    size_t cap = 0;
    if (!g_sink.acquire(&slot, &cap) || cap == 0) {
      g_ring_drops.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    size_t n = std::min(bytes, cap);
    n &= ~static_cast<size_t>(1);  // whole I/Q pairs
    if (n == 0) {
      g_sink.release(slot);
      return;
    }
    std::memcpy(slot, data, n);
    g_sink.submit(slot, n);
    data += n;
    bytes -= n;
  }
}

void reader_task(void*) {
  int64_t not_streaming_since = 0;
  uint32_t last_seq = 0, last_gen = 0;
  bool have_seq = false;
  while (g_reader_run.load(std::memory_order_acquire)) {
    pluto_sdr_block_t meta{};
    const int n = pluto_sdr_read(g_h, g_stage, g_stage_cap, 100, &meta);
    if (n > 0) {
      if (have_seq && (meta.sequence != last_seq + 1 || meta.generation != last_gen)) g_sink.discontinuity();  // a gap in the sample sequence breaks demodulator state
      last_seq = meta.sequence;
      last_gen = meta.generation;
      have_seq = true;
      g_first_block.store(true, std::memory_order_release);
      deliver(g_stage, static_cast<size_t>(n));
    }
    const pluto_sdr_state_t st = pluto_sdr_get_state(g_h);
    const int64_t now = esp_timer_get_time();
    if (st == PLUTO_SDR_STATE_STREAMING || st == PLUTO_SDR_STATE_OPENING) {
      not_streaming_since = 0;
      g_alive.store(true, std::memory_order_release);
    } else {
      if (!not_streaming_since) not_streaming_since = now;
      if (now - not_streaming_since > kQuietRecoveryUs) g_alive.store(false, std::memory_order_release);
    }
  }
  g_reader_exited.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

// Remembers across reboots that a Pluto has been used here, so a board left wedged by a Tab5 reset is still recovered after the new boot.
// Cleared when the ladder gives up, so a Pluto that was removed for good does not make every boot cycle the port.
bool seen_flag_get() {
  nvs_handle_t nvs;
  uint8_t v = 0;
  if (nvs_open("orcpluto", NVS_READONLY, &nvs) != ESP_OK) return false;
  (void)nvs_get_u8(nvs, "seen", &v);
  nvs_close(nvs);
  return v != 0;
}

void seen_flag_set(bool seen) {
  nvs_handle_t nvs;
  if (nvs_open("orcpluto", NVS_READWRITE, &nvs) != ESP_OK) return;
  (void)nvs_set_u8(nvs, "seen", seen ? 1 : 0);
  (void)nvs_commit(nvs);
  nvs_close(nvs);
}

std::atomic<int8_t> g_seen_cache{-1};              // -1 = not read yet
std::atomic<int64_t> g_hold_off_until_us{0};        // the ladder waits until this time (the boot sequence just cycled the rail)
constexpr int64_t kBootHoldOffUs = 60000000;        // 25 s boot plus enumeration plus margin

void note_healthy() {
  g_start_failures.store(0, std::memory_order_release);
  g_cycles.store(0, std::memory_order_release);
  g_gave_up.store(false, std::memory_order_release);
}

// One rung of the ladder: runs the application's power-cycle hook if the backoff allows it.
void try_recovery(const char* reason, int64_t now_us, int64_t* next_cycle_us) {
  if (!g_events.power_cycle || g_gave_up.load(std::memory_order_acquire) || now_us < *next_cycle_us ||
      now_us < g_hold_off_until_us.load(std::memory_order_acquire))
    return;
  const uint32_t done = g_cycles.load(std::memory_order_acquire);
  if (done >= kMaxCycles) {
    g_gave_up.store(true, std::memory_order_release);
    seen_flag_set(false);
    g_seen_cache.store(0, std::memory_order_release);
    ESP_LOGE(TAG, "PLUTO_RECOVERY gave_up cycles=%u: the board needs a manual power cycle", static_cast<unsigned>(done));
    return;
  }
  const bool ran = g_events.power_cycle();
  const uint32_t n = ran ? g_cycles.fetch_add(1, std::memory_order_acq_rel) + 1 : done;
  const size_t step = std::min<size_t>(done, sizeof(kBackoffUs) / sizeof(kBackoffUs[0]) - 1);
  *next_cycle_us = esp_timer_get_time() + kBackoffUs[step];
  g_start_failures.store(0, std::memory_order_release);
  ESP_LOGW(TAG, "PLUTO_RECOVERY reason=%s power_cycle=%s cycle=%u/%u next_in_s=%d", reason, ran ? "done" : "skipped", static_cast<unsigned>(n),
           static_cast<unsigned>(kMaxCycles), static_cast<int>(kBackoffUs[step] / 1000000));
}

// One line a minute for soak tests and field logs: stream state, counters and the heap numbers that have mattered on this board.
constexpr int64_t kHealthEveryUs = 60000000;
void log_health(bool present) {
  pluto_sdr_metrics_t m{};
  if (g_h) pluto_sdr_get_metrics(g_h, &m);
  std::printf("PLUTO_HEALTH present=%d running=%d alive=%d state=%d samples=%llu src_drops=%u usb_err=%u ring_drops=%u cycles=%u int_free=%u dma_largest=%u psram_free=%u\n",
              present ? 1 : 0, g_running.load(std::memory_order_relaxed) ? 1 : 0, g_alive.load(std::memory_order_relaxed) ? 1 : 0,
              g_h ? static_cast<int>(pluto_sdr_get_state(g_h)) : -1, static_cast<unsigned long long>(m.samples_delivered),
              static_cast<unsigned>(m.source_drops), static_cast<unsigned>(m.usb_errors), static_cast<unsigned>(g_ring_drops.load(std::memory_order_relaxed)),
              static_cast<unsigned>(g_cycles.load(std::memory_order_relaxed)),
              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
              static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)),
              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void watcher_task(void*) {
  int64_t last_health_us = 0;
  bool seen_before = seen_flag_get();
  int64_t lost_at = esp_timer_get_time(), next_cycle_us = 0;  // a Pluto used before this boot gets the same grace period from boot
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    const bool now = pluto_sdr_device_present(g_h);
    const bool was = g_present.exchange(now, std::memory_order_acq_rel);
    const int64_t t = esp_timer_get_time();
    if (t - last_health_us >= kHealthEveryUs) {
      last_health_us = t;
      log_health(now);
    }
    if (now && !was) {
      ESP_LOGI(TAG, "PlutoSDR attached");
      if (!seen_before) {
        seen_flag_set(true);
        g_seen_cache.store(1, std::memory_order_release);
      }
      seen_before = true;
      if (g_events.attached) g_events.attached();
    } else if (!now && was) {
      ESP_LOGW(TAG, "PlutoSDR removed");
      lost_at = t;
      if (g_events.detached) g_events.detached();
    }
    if (g_running.load(std::memory_order_acquire)) {
      if (g_first_block.load(std::memory_order_acquire)) note_healthy();
      continue;
    }
    if (!now && seen_before && lost_at && t - lost_at > kLostGraceUs) {
      try_recovery("not_returning", t, &next_cycle_us);
    } else if (now) {
      const uint32_t fails = g_start_failures.load(std::memory_order_acquire);
      const int64_t since = t - g_last_failure_us.load(std::memory_order_acquire);
      if (fails >= 3 || (fails >= 1 && since > kFailedStartGraceUs)) try_recovery("start_failures", t, &next_cycle_us);
    }
  }
}

}  // namespace

bool begin(const IqSink& sink, const Events& events) {
  if (g_h) return true;
  g_sink = sink;
  g_events = events;
  pluto_sdr_config_t cfg;
  pluto_sdr_config_default(&cfg);
  cfg.install_usb_host = false;  // the RTL-SDR driver owns the USB host library; the Pluto driver adds one more client
  cfg.format = PLUTO_SDR_FORMAT_CU8;
  pluto_sdr_handle_t h = nullptr;
  if (pluto_sdr_install(&cfg, &h) != PLUTO_SDR_OK) {
    ESP_LOGE(TAG, "Pluto driver install failed");
    return false;
  }
  g_stage_cap = pluto_sdr_max_block_bytes(h);
  g_stage = static_cast<uint8_t*>(heap_caps_malloc(g_stage_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_stage) {
    ESP_LOGE(TAG, "IQ staging buffer alloc failed");
    pluto_sdr_uninstall(h);
    return false;
  }
  g_h = h;
  if (xTaskCreatePinnedToCore(watcher_task, "pluto_watch", 4096, nullptr, 3, nullptr, 0) != pdPASS) {
    ESP_LOGE(TAG, "watcher task failed");
    return false;
  }
  ESP_LOGI(TAG, "PlutoSDR source ready (esp-pluto-sdr %s, receive only)", pluto_sdr_get_version_string());
  return true;
}

bool previously_used() {
  int8_t v = g_seen_cache.load(std::memory_order_acquire);
  if (v < 0) {
    v = seen_flag_get() ? 1 : 0;
    g_seen_cache.store(v, std::memory_order_release);
  }
  return v != 0;
}

void note_power_cycled() { g_hold_off_until_us.store(esp_timer_get_time() + kBootHoldOffUs, std::memory_order_release); }

bool ready() { return g_h != nullptr && g_present.load(std::memory_order_acquire); }
bool active() { return g_running.load(std::memory_order_acquire); }

static bool start_once(uint32_t sample_rate_sps, uint32_t frequency_hz, uint32_t timeout_ms, bool (*cancelled)()) {
  if (!g_h || g_running.load(std::memory_order_acquire)) return false;
  if (pluto_sdr_set_sample_rate(g_h, sample_rate_sps) != PLUTO_SDR_OK) {
    ESP_LOGE(TAG, "rate %u refused", static_cast<unsigned>(sample_rate_sps));
    return false;
  }
  if (pluto_sdr_set_center_freq(g_h, frequency_hz) != PLUTO_SDR_OK) {
    ESP_LOGE(TAG, "frequency %u refused", static_cast<unsigned>(frequency_hz));
    return false;
  }
  g_first_block.store(false, std::memory_order_release);
  g_alive.store(true, std::memory_order_release);
  g_running.store(true, std::memory_order_release);
  if (pluto_sdr_start(g_h) != PLUTO_SDR_OK) {
    g_running.store(false, std::memory_order_release);
    return false;
  }
  g_reader_run.store(true, std::memory_order_release);
  g_reader_exited.store(false, std::memory_order_release);
  if (xTaskCreatePinnedToCore(reader_task, "pluto_rx", 6144, nullptr, 5, nullptr, 0) != pdPASS) {
    g_reader_exited.store(true, std::memory_order_release);
    g_reader_run.store(false, std::memory_order_release);
    pluto_sdr_stop(g_h, 3000);
    g_running.store(false, std::memory_order_release);
    return false;
  }
  const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
  while (!g_first_block.load(std::memory_order_acquire)) {
    if ((cancelled && cancelled()) || esp_timer_get_time() > deadline) {
      stop();
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  return true;
}

bool start_and_wait(uint32_t sample_rate_sps, uint32_t frequency_hz, uint32_t timeout_ms, bool (*cancelled)()) {
  const bool ok = start_once(sample_rate_sps, frequency_hz, timeout_ms, cancelled);
  if (ok) {
    note_healthy();
  } else if (g_present.load(std::memory_order_acquire) && !(cancelled && cancelled())) {
    g_last_failure_us.store(esp_timer_get_time(), std::memory_order_release);  // a cancelled start is the user's doing, not a fault
    g_start_failures.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

bool alive() { return g_running.load(std::memory_order_acquire) && g_alive.load(std::memory_order_acquire); }

void stop() {
  if (!g_h) return;
  g_reader_run.store(false, std::memory_order_release);
  for (int i = 0; i < 100 && !g_reader_exited.load(std::memory_order_acquire); ++i) vTaskDelay(pdMS_TO_TICKS(20));
  pluto_sdr_stop(g_h, 3000);
  g_running.store(false, std::memory_order_release);
  g_alive.store(false, std::memory_order_release);
}

bool retune(uint32_t frequency_hz) { return g_h && pluto_sdr_set_center_freq(g_h, frequency_hz) == PLUTO_SDR_OK; }
bool set_gain_auto() { return g_h && pluto_sdr_set_gain_auto(g_h) == PLUTO_SDR_OK; }
bool set_gain_manual_tenths_db(int tenths_db) { return g_h && pluto_sdr_set_gain_manual(g_h, tenths_db) == PLUTO_SDR_OK; }

Gain gain() {
  Gain g;
  if (!g_h) return g;
  pluto_sdr_gain_t pg;
  pluto_sdr_caps_t caps;
  if (pluto_sdr_get_gain(g_h, &pg) != PLUTO_SDR_OK) return g;
  pluto_sdr_get_capabilities(g_h, &caps);
  g.known = true;
  g.manual = pg.manual;
  g.tenths_db = pg.gain_db10;
  if (caps.valid) {
    g.min_tenths_db = caps.gain_min_db10;
    g.max_tenths_db = caps.gain_max_db10;
  }
  g.step_count = std::min<uint8_t>(pg.step_count, 32);
  for (int i = 0; i < g.step_count; ++i) g.steps[i] = pg.steps_db10[i];
  return g;
}

Status status() {
  Status s;
  s.present = ready();
  std::snprintf(s.version, sizeof(s.version), "%s", pluto_sdr_get_version_string());
  if (!g_h) return s;
  s.streaming = pluto_sdr_get_state(g_h) == PLUTO_SDR_STATE_STREAMING;
  s.frequency_hz = clamp32(pluto_sdr_get_center_freq(g_h));
  s.sample_rate_sps = pluto_sdr_get_sample_rate(g_h);
  pluto_sdr_caps_t c;
  pluto_sdr_info_t info;
  pluto_sdr_metrics_t m;
  pluto_sdr_get_capabilities(g_h, &c);
  pluto_sdr_get_device_info(g_h, &info);
  pluto_sdr_get_metrics(g_h, &m);
  if (c.valid) {
    s.tune_min_hz = clamp32(c.tune_min_hz);
    s.tune_max_hz = clamp32(c.tune_max_hz);
  }
  s.samples_delivered = m.samples_delivered;
  s.source_drops = m.source_drops;
  s.usb_errors = m.usb_errors;
  s.ring_drops = g_ring_drops.load(std::memory_order_relaxed);
  s.recovery_cycles = g_cycles.load(std::memory_order_relaxed);
  s.recovery_gave_up = g_gave_up.load(std::memory_order_relaxed);
  std::snprintf(s.product, sizeof(s.product), "%s", info.product[0] ? info.product : "PlutoSDR AD9361");
  std::snprintf(s.serial, sizeof(s.serial), "%s", info.serial);
  return s;
}

}  // namespace orcsdr::pluto_source
