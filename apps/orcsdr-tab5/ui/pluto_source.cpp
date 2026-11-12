// PlutoSDR local receiver source (see pluto_source.hpp). The device logic is in the esp-pluto-sdr driver; this file watches for the device,
// starts and stops its stream on the application's request, and copies the driver's blocks into the application's IQ ring slots.
#include "pluto_source.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
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

void watcher_task(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    const bool now = pluto_sdr_device_present(g_h);
    const bool was = g_present.exchange(now, std::memory_order_acq_rel);
    if (now && !was) {
      ESP_LOGI(TAG, "PlutoSDR attached");
      if (g_events.attached) g_events.attached();
    } else if (!now && was) {
      ESP_LOGW(TAG, "PlutoSDR removed");
      if (g_events.detached) g_events.detached();
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

bool ready() { return g_h != nullptr && g_present.load(std::memory_order_acquire); }
bool active() { return g_running.load(std::memory_order_acquire); }

bool start_and_wait(uint32_t sample_rate_sps, uint32_t frequency_hz, uint32_t timeout_ms, bool (*cancelled)()) {
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
  std::snprintf(s.product, sizeof(s.product), "%s", info.product[0] ? info.product : "PlutoSDR AD9361");
  std::snprintf(s.serial, sizeof(s.serial), "%s", info.serial);
  return s;
}

}  // namespace orcsdr::pluto_source
