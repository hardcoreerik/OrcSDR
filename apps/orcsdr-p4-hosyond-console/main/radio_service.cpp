#include "radio_service.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rtl_sdr.h"
#include "esp_timer.h"
#include "freertos/message_buffer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "home_4b_layout.hpp"
#include "home_fft.hpp"
#include "sdkconfig.h"

using namespace orcsdr::waveshare4b;

#ifndef CONFIG_ORCSDR_MAX_RECEIVERS
#define CONFIG_ORCSDR_MAX_RECEIVERS 1
#endif

namespace orcsdr::radio {
namespace {
constexpr uint32_t kRate = 960000;      // default rate
constexpr uint32_t kWideRate = 2400000;  // Home's wide spans
constexpr size_t kIqBytes = 16384;
constexpr int kRx = CONFIG_ORCSDR_MAX_RECEIVERS < 1 ? 1 : (CONFIG_ORCSDR_MAX_RECEIVERS > kMaxReceivers ? kMaxReceivers : CONFIG_ORCSDR_MAX_RECEIVERS);
// One receiver keeps the transfer depth this firmware always used. Several receivers share the internal USB DMA pool, so
// each gets the two 16 KiB transfers the four-dongle validation (OrcNode, 2.56 MS/s, 120 s, zero drops) was run with.
constexpr size_t kTransfersSingle = 6, kTransfersMulti = 2;

// Header sent in front of each block to a queued consumer.
struct FrameHeader {
  uint32_t sample_rate, center_hz, sequence, bytes, generation;
  int64_t time_us;
  uint8_t receiver;
};
constexpr size_t kFrameBytes = sizeof(FrameHeader) + kIqBytes;

struct Sink {
  std::atomic<int> state{0};  // 0 free, 1 active, 2 removing (or quarantined: never reused), 3 being set up
  SemaphoreHandle_t lock = nullptr;  // held by the radio loop while it calls or feeds this sink; removal takes it too
  IqConsumer fn = nullptr;
  void* ctx = nullptr;
  SinkKind kind = SinkKind::queued;
  char name[16] = {};
  MessageBufferHandle_t mb = nullptr;
  TaskHandle_t task = nullptr;
  std::atomic<bool> run{false}, exited{true};
  uint8_t receiver = 0;
  std::atomic<uint32_t> blocks{0}, drops{0}, max_us{0};
};

struct Rx {
  int index = 0;
  char tag[8] = "RADIO";
  std::atomic<uint32_t> requested{kDefaultHz}, applied{0}, sps{0}, drops{0}, step_idx{2};
  std::atomic<int> state{0};  // 0 no receiver, 1 scanning, 2 streaming, 3 error
  std::atomic<uint32_t> tune_request{0};
  std::atomic<uint32_t> rate_target{kRate}, rate_now{kRate}, rate_override{0};
  std::atomic<float> dbfs{-120.0f};
  Info info;
  Gain gain;  // cache, guarded by gain_lock
  portMUX_TYPE gain_lock = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<int> gain_req_kind{0};  // 0 none, 1 auto, 2 manual, 3 rtl agc, 4 bias, 5 ppm, 6 bandwidth
  std::atomic<int> gain_req_value{0};
  portMUX_TYPE bands_lock = portMUX_INITIALIZER_UNLOCKED;
  uint8_t bands[kFftSize]{};
  uint32_t band_seq = 0, taken_seq = 0;
  Sink sinks[kMaxSinks];
  std::atomic<uint32_t> generation{1};  // +1 on every stream start, stop, rate change or recovery (see IqBlock::generation)
  std::atomic<uint32_t> sequence{0};    // +1 per block read
  TaskHandle_t task_handle = nullptr;
  uint8_t* frame = nullptr;           // scratch copy of the block, shared by every queued consumer
};

QueueHandle_t queue = nullptr;  // Home's commands go to receiver 0 only
Rx g_rx[kRx];
std::atomic<bool> g_host_ready{false};  // receiver 0 has installed the USB host library
SemaphoreHandle_t g_start_mutex = nullptr;  // one receiver at a time claims a dongle

// ---- sink registry -----------------------------------------------------------------------------------------------

void queued_task(void* arg) {
  Sink* s = static_cast<Sink*>(arg);
  auto* buf = static_cast<uint8_t*>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  while (s->run.load() && buf) {
    const size_t got = xMessageBufferReceive(s->mb, buf, kFrameBytes, pdMS_TO_TICKS(50));
    if (got < sizeof(FrameHeader)) continue;
    FrameHeader h;
    std::memcpy(&h, buf, sizeof(h));
    const IqBlock b{buf + sizeof(FrameHeader), h.bytes, h.sample_rate, h.center_hz, h.sequence, h.time_us, h.receiver, h.generation};
    const int64_t t0 = esp_timer_get_time();
    s->fn(b, s->ctx);
    const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    if (us > s->max_us.load()) s->max_us = us;
    s->blocks.fetch_add(1);
  }
  heap_caps_free(buf);
  s->exited = true;
  vTaskDelete(nullptr);
}

void dispatch(Rx& r, const uint8_t* iq, size_t n, int64_t now) {
  const uint32_t seq = r.sequence.fetch_add(1);
  const uint32_t rate = r.rate_now.load(), hz = r.applied.load() ? r.applied.load() : r.requested.load();
  const uint32_t gen = r.generation.load();
  const IqBlock b{iq, n, rate, hz, seq, now, static_cast<uint8_t>(r.index), gen};
  bool framed = false;
  for (Sink& s : r.sinks) {
    if (s.state.load(std::memory_order_acquire) != 1) continue;
    // The lock is held for the whole call (or send), so remove_sink cannot finish while this sink is in use. If a removal already
    // holds the lock we skip the sink this block; the state is re-read under the lock so a sink removed in between is not called.
    if (xSemaphoreTake(s.lock, 0) != pdTRUE) continue;
    if (s.state.load(std::memory_order_acquire) == 1) {
      if (s.kind == SinkKind::inline_fast) {
        const int64_t t0 = esp_timer_get_time();
        s.fn(b, s.ctx);
        const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
        if (us > s.max_us.load()) s.max_us = us;
        s.blocks.fetch_add(1);
      } else if (r.frame && s.mb) {
        if (!framed) {
          const FrameHeader h{rate, hz, seq, static_cast<uint32_t>(n), gen, now, static_cast<uint8_t>(r.index)};
          std::memcpy(r.frame, &h, sizeof(h));
          std::memcpy(r.frame + sizeof(h), iq, n);
          framed = true;
        }
        if (xMessageBufferSend(s.mb, r.frame, sizeof(FrameHeader) + n, 0) == 0) s.drops.fetch_add(1);
      }
    }
    xSemaphoreGive(s.lock);
  }
}

// ---- receiver task -----------------------------------------------------------------------------------------------

void task(void* arg) {
  Rx& r = *static_cast<Rx*>(arg);
  const char* TAG = r.tag;
  auto* iq = static_cast<uint8_t*>(heap_caps_malloc(kIqBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  r.frame = static_cast<uint8_t*>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!iq || !r.frame) {
    r.state = 3;
    ESP_LOGE(TAG, "IQ buffer alloc failed");
    vTaskDelete(nullptr);
    return;
  }
  // A second receiver cannot install until receiver 0 has set up the shared USB host library.
  while (r.index > 0 && !g_host_ready.load()) vTaskDelay(pdMS_TO_TICKS(200));
  esp_rtl_sdr_config_t cfg;
  esp_rtl_sdr_config_default(&cfg);
  cfg.host_library_already_installed = r.index > 0;  // one USB host owner: esp-rtl-sdr, installed by receiver 0
  cfg.delivery_mode = ESP_RTL_SDR_DELIVERY_READ;
  cfg.transfer_count = kRx > 1 ? kTransfersMulti : kTransfersSingle;
  cfg.transfer_bytes = kIqBytes;
  if (kRx > 1) cfg.usb_task_core_id = static_cast<uint8_t>(r.index % 2);  // split USB clients across both cores
  cfg.bind_device_index = ESP_RTL_SDR_BIND_ANY;  // first USB address no other handle owns
  esp_rtl_sdr_handle_t sdr = nullptr;
  const esp_err_t install = esp_rtl_sdr_install(&cfg, &sdr);
  if (install != ESP_OK) {
    ESP_LOGE(TAG, "SDR install failed: %s", esp_rtl_sdr_err_to_name(install));
    r.state = 3;
    heap_caps_free(iq);
    vTaskDelete(nullptr);
    return;
  }
  if (r.index == 0) g_host_ready = true;
  bool streaming = false;
  int64_t last_scan = 0, last_fft = 0, last_metrics = 0, last_stream_start_us = 0, last_gain_poll = 0;
  uint32_t scans = 0, last_seen_target = 0;
  int64_t target_changed_at = 0;
  for (;;) {
    const uint32_t absolute = r.tune_request.exchange(0);
    if (absolute) {
      r.requested = absolute;
      ESP_LOGI(TAG, "TUNE requested_hz=%u streaming=%d", unsigned(absolute), int(streaming));
      if (streaming) {
        const esp_err_t e = esp_rtl_sdr_retune_hz(sdr, absolute);
        if (e != ESP_OK) ESP_LOGE(TAG, "retune failed: %s", esp_rtl_sdr_err_to_name(e));
      }
    }
    home_v3::Command cmd;
    while (r.index == 0 && queue && xQueueReceive(queue, &cmd, 0) == pdTRUE) {
      if (cmd == home_v3::Command::Step) {
        r.step_idx = (r.step_idx.load() + 1) % 6;
        continue;
      }
      uint32_t next = r.requested.load();
      switch (cmd) {
        case home_v3::Command::Down: next = tune(next, -1, kSteps[r.step_idx.load()]); break;
        case home_v3::Command::Up: next = tune(next, 1, kSteps[r.step_idx.load()]); break;
        case home_v3::Command::FM: next = 96100000; break;
        case home_v3::Command::WX: next = 162400000; break;
        case home_v3::Command::Air: next = 125000000; break;
        case home_v3::Command::Ham: next = 145120000; break;
        default: break;
      }
      r.requested = next;
      ESP_LOGI(TAG, "TUNE requested_hz=%u streaming=%d", unsigned(next), int(streaming));
      if (streaming) {
        const esp_err_t e = esp_rtl_sdr_retune_hz(sdr, next);
        if (e != ESP_OK) ESP_LOGE(TAG, "retune failed: %s", esp_rtl_sdr_err_to_name(e));
      }
    }
    const int64_t now = esp_timer_get_time();
    if (!streaming) {
      r.applied = 0;
      r.sps = 0;
      if (now - last_scan > 1000000) {
        last_scan = now;
        r.state = 1;
        // Claiming a dongle is serialized across receivers: two handles must not take the same one.
        if (g_start_mutex) xSemaphoreTake(g_start_mutex, portMAX_DELAY);
        size_t count = 0, raw_count = 0;
        const esp_err_t raw_result = esp_rtl_sdr_usb_device_count(&raw_count);
        const esp_err_t refresh_result = esp_rtl_sdr_refresh_device_list(sdr);
        const esp_err_t count_result =
            refresh_result == ESP_OK ? esp_rtl_sdr_get_device_count(sdr, &count) : refresh_result;
        // The raw USB count includes hubs and unsupported devices; an empty candidate list
        // is not by itself evidence that the board supplies VBUS.
        if ((++scans % 5u) == 1u || raw_count || count) {
          ESP_LOGI(TAG, "USB_DIAG raw_devices=%u accepted_sdr=%u raw=%s scan=%s safe_mode=%u",
                   unsigned(raw_count), unsigned(count), esp_rtl_sdr_err_to_name(raw_result),
                   esp_rtl_sdr_err_to_name(count_result), unsigned(esp_rtl_sdr_usb_safe_mode_active()));
        }
        // Receiver k takes a dongle only when more than k are attached (the first free address goes to the first
        // receiver that asks), the rule the OrcNode receiver manager used for its four-dongle runs.
        if (refresh_result == ESP_OK && count_result == ESP_OK && count > static_cast<size_t>(r.index)) {
          esp_rtl_sdr_stream_config_t sc;
          esp_rtl_sdr_stream_config_default(&sc);
          sc.preset = ESP_RTL_SDR_PRESET_CUSTOM_HZ;
          sc.frequency_hz = r.requested.load();
          sc.sample_rate_sps = r.rate_target.load();
          const esp_err_t e = esp_rtl_sdr_start(sdr, &sc);
          if (e == ESP_OK) {
            streaming = true;
            r.generation.fetch_add(1);
            r.state = 2;
            last_stream_start_us = now;
            r.rate_now = sc.sample_rate_sps;
            ESP_LOGI(TAG, "RF START hz=%u rate=%u", unsigned(sc.frequency_hz), unsigned(sc.sample_rate_sps));
          } else {
            ESP_LOGE(TAG, "start failed: %s", esp_rtl_sdr_err_to_name(e));
            r.state = 3;
            // A failed start can leave the driver in FAULT; clear it (stop, then reset) so the next scan can retry.
            (void)esp_rtl_sdr_stop(sdr, 1000);
            const esp_err_t rs = esp_rtl_sdr_reset(sdr);
            ESP_LOGW(TAG, "driver reset after failed start: %s (state %s)", esp_err_to_name(rs),
                     esp_rtl_sdr_state_to_name(esp_rtl_sdr_get_state(sdr)));
          }
        } else {
          r.state = 0;
        }
        if (g_start_mutex) xSemaphoreGive(g_start_mutex);
      }
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    // A rate change is a USB stop/start. Wait until the requested rate has been steady for a moment (screens often ask
    // twice in quick succession) and never restart twice within 1.5 s: back-to-back restarts left the USB device stuck.
    const uint32_t wanted_rate = r.rate_target.load();
    if (wanted_rate != last_seen_target) {
      last_seen_target = wanted_rate;
      target_changed_at = now;
    }
    if (wanted_rate != r.rate_now.load() && now - target_changed_at > 400000 && now - last_stream_start_us > 1500000) {
      // The driver changes sample rate only across stop/start.
      ESP_LOGI(TAG, "sample rate %u -> %u", unsigned(r.rate_now.load()), unsigned(r.rate_target.load()));
      r.generation.fetch_add(1);
      (void)esp_rtl_sdr_stop(sdr, 1500);
      streaming = false;
      r.state = 1;
      last_scan = 0;
      continue;
    }
    if (esp_rtl_sdr_get_state(sdr) != ESP_RTL_SDR_STATE_STREAMING) {
      ESP_LOGW(TAG, "stream ended (dongle unplugged or faulted), rescanning for it");
      (void)esp_rtl_sdr_stop(sdr, 1500);
      // Clear a FAULT left behind by the disconnect so the next start is not refused.
      const esp_err_t reset = esp_rtl_sdr_reset(sdr);
      ESP_LOGI(TAG, "driver reset after stream end: %s", esp_err_to_name(reset));
      streaming = false;
      r.generation.fetch_add(1);
      r.state = 0;
      r.applied = 0;
      r.sps = 0;
      last_scan = 0;  // look again straight away
      continue;
    }
    size_t n = 0;
    const esp_err_t e = esp_rtl_sdr_read(sdr, iq, kIqBytes, 40, &n);
    if (e == ESP_OK && n) dispatch(r, iq, n, now);
    if (e == ESP_OK && n >= 2048 && now - last_fft > 50000) {
      // Level of the block as RMS of the centred 8-bit samples, in dBFS.
      // Single precision: this chip's FPU is float-only, so doubles here cost a software-emulated 14% of the core.
      float acc = 0.0f;
      const size_t count = n < 4096 ? n : 4096;
      for (size_t i = 0; i < count; ++i) {
        const float v = (static_cast<float>(iq[i]) - 127.5f) * (1.0f / 127.5f);
        acc += v * v;
      }
      const float rms = std::sqrt(acc / static_cast<float>(count));
      r.dbfs = rms > 1e-6f ? 20.0f * std::log10(rms) : -120.0f;
    }
    if (e == ESP_OK && n >= kFftSize * 2 && now - last_fft > 50000) {
      Spectrum view = spectrum_from_cu8(iq, n);
      if (view.real_samples) {
        portENTER_CRITICAL(&r.bands_lock);
        memcpy(r.bands, view.values.data(), kFftSize);
        ++r.band_seq;
        portEXIT_CRITICAL(&r.bands_lock);
        last_fft = now;
      }
    } else if (e != ESP_OK && e != ESP_RTL_SDR_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "read error %s", esp_rtl_sdr_err_to_name(e));
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    {
      // Apply a queued control change, then refresh the cache (about twice a second).
      const int kind = r.gain_req_kind.exchange(0);
      if (kind) {
        const int value = r.gain_req_value.load();
        esp_err_t rr = ESP_OK;
        if (kind == 1) rr = esp_rtl_sdr_set_tuner_gain_mode(sdr, ESP_RTL_SDR_GAIN_MODE_AUTO);
        else if (kind == 2) {
          rr = esp_rtl_sdr_set_tuner_gain_mode(sdr, ESP_RTL_SDR_GAIN_MODE_MANUAL);
          if (rr == ESP_OK) rr = esp_rtl_sdr_set_tuner_gain(sdr, value);
        } else if (kind == 3) rr = esp_rtl_sdr_set_rtl_agc(sdr, value != 0);
        else if (kind == 4) rr = esp_rtl_sdr_set_bias_tee(sdr, value != 0);
        else if (kind == 5) rr = esp_rtl_sdr_set_freq_correction(sdr, value);
        else if (kind == 6) rr = esp_rtl_sdr_set_tuner_bandwidth(sdr, static_cast<uint32_t>(value));
        ESP_LOGI(TAG, "GAIN request kind=%d value=%d -> %s", kind, value, esp_rtl_sdr_err_to_name(rr));
        last_gain_poll = 0;
      }
      if (now - last_gain_poll > 500000) {
        last_gain_poll = now;
        Gain g;
        const uint32_t caps = esp_rtl_sdr_get_capabilities();
        g.available = (caps & ESP_RTL_SDR_CAP_GAIN) != 0;
        g.auto_available = (caps & ESP_RTL_SDR_CAP_GAIN_AUTO) != 0;
        g.rtl_agc_available = (caps & ESP_RTL_SDR_CAP_RTL_AGC) != 0;
        g.bias_available = (caps & ESP_RTL_SDR_CAP_BIAS_TEE) != 0;
        esp_rtl_sdr_gain_mode_t mode = ESP_RTL_SDR_GAIN_MODE_AUTO;
        if (esp_rtl_sdr_get_tuner_gain_mode(sdr, &mode) == ESP_OK) g.auto_mode = mode == ESP_RTL_SDR_GAIN_MODE_AUTO;
        (void)esp_rtl_sdr_get_tuner_gain(sdr, &g.tenth_db);
        size_t count = 0;
        if (esp_rtl_sdr_get_tuner_gains(sdr, g.steps, 32, &count) == ESP_OK) g.step_count = static_cast<uint8_t>(count < 32 ? count : 32);
        (void)esp_rtl_sdr_get_rtl_agc(sdr, &g.rtl_agc);
        (void)esp_rtl_sdr_get_bias_tee(sdr, &g.bias);
        Info inf;
        inf.caps = caps;
        (void)esp_rtl_sdr_get_freq_correction(sdr, &inf.ppm);
        if (caps & ESP_RTL_SDR_CAP_TUNER_BANDWIDTH) {
          (void)esp_rtl_sdr_get_tuner_bandwidth_state(sdr, &inf.bw_requested_hz, &inf.bw_applied_hz);
          size_t bc = 0;
          if (esp_rtl_sdr_get_tuner_bandwidths(sdr, inf.bw_list, 16, &bc) == ESP_OK) inf.bw_count = static_cast<uint8_t>(bc < 16 ? bc : 16);
        }
        esp_rtl_sdr_metrics_t mm{};
        if (esp_rtl_sdr_get_metrics(sdr, &mm) == ESP_OK) {
          inf.overruns = mm.overruns;
          inf.consumer_drops = mm.consumer_drops;
        }
        esp_rtl_sdr_device_info_t di{};
        if (esp_rtl_sdr_get_device_info(sdr, &di) == ESP_OK && di.present)
          std::snprintf(inf.device, sizeof(inf.device), "%.30s %.12s", di.product, di.serial);
        else
          std::snprintf(inf.device, sizeof(inf.device), "RTL-SDR");
        esp_rtl_sdr_identity_t id{};
        id.struct_size = sizeof(id);
        if (esp_rtl_sdr_get_identity(sdr, &id) == ESP_OK && id.present)
          std::snprintf(inf.usb_path, sizeof(inf.usb_path), "%.23s", id.usb_path[0] ? id.usb_path : "?");
        esp_rtl_sdr_health_info_t hi{};
        if (esp_rtl_sdr_get_health(sdr, &hi) == ESP_OK)
          std::snprintf(inf.health, sizeof(inf.health), "%.27s", hi.advice[0] ? hi.advice : "OK");
        else
          std::snprintf(inf.health, sizeof(inf.health), "unknown");
        std::snprintf(inf.version, sizeof(inf.version), "%s", esp_rtl_sdr_get_version_string());
        portENTER_CRITICAL(&r.gain_lock);
        r.info = inf;
        r.gain = g;
        portEXIT_CRITICAL(&r.gain_lock);
      }
    }
    if (now - last_metrics > 750000) {
      last_metrics = now;
      esp_rtl_sdr_metrics_t m{};
      if (esp_rtl_sdr_get_metrics(sdr, &m) == ESP_OK) {
        r.applied = m.frequency_hz;
        r.sps = m.effective_sps;
        r.drops = m.slot_starve_blocks + m.ring_overrun_blocks + m.pull_lock_miss_blocks;
        ESP_LOGI(TAG, "IQ hz=%u sps=%u consumed=%llu drops=%u", unsigned(m.frequency_hz),
                 unsigned(m.effective_sps), (unsigned long long)m.bytes_consumed, unsigned(r.drops.load()));
      }
    }
  }
}

Rx* rx_at(int rx) { return rx >= 0 && rx < kRx ? &g_rx[rx] : nullptr; }

// Receiver 0 owns the old, un-indexed calls; the two legacy sink slots are ordinary registry entries.
SinkHandle g_legacy[2] = {-1, -1};
void legacy_adapter(const IqBlock& b, void* ctx) { reinterpret_cast<IqSink>(ctx)(b.data, b.bytes); }
void set_legacy(int which, IqSink sink) {
  if (g_legacy[which] >= 0) {
    // Legacy sinks hold only a function pointer, so a quarantined slot costs one of the eight slots but frees nothing unsafely.
    if (!remove_sink(g_legacy[which])) ESP_LOGE("RADIO", "legacy %s sink could not be removed cleanly", which == 0 ? "primary" : "aux");
    g_legacy[which] = -1;
  }
  if (!sink) return;
  SinkSpec spec;
  spec.name = which == 0 ? "primary" : "aux";
  spec.fn = legacy_adapter;
  spec.context = reinterpret_cast<void*>(sink);
  spec.kind = SinkKind::inline_fast;  // the old contract: called from the receiver task
  g_legacy[which] = add_sink(spec);
}
}  // namespace

bool start(QueueHandle_t commands) {
  queue = commands;
  g_start_mutex = xSemaphoreCreateMutex();
  ESP_LOGI("RADIO", "driver=%s psram_free=%u receivers=%d", esp_rtl_sdr_get_version_string(),
           unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), kRx);
  bool ok = true;
  for (int i = 0; i < kRx; ++i) {
    Rx& r = g_rx[i];
    r.index = i;
    if (i > 0) std::snprintf(r.tag, sizeof(r.tag), "RADIO%d", i);
    char name[8] = "radio";
    if (i > 0) std::snprintf(name, sizeof(name), "radio%d", i);
    ok = xTaskCreatePinnedToCore(task, name, 8192, &r, 5, &r.task_handle, 1) == pdPASS && ok;
  }
  return ok;
}

int max_receivers() { return kRx; }
int receiver_count() {
  int n = 0;
  for (int i = 0; i < kRx; ++i) n += g_rx[i].state.load() == 2 ? 1 : 0;
  return n;
}
bool receiver_info(int rx, ReceiverInfo* out) {
  Rx* r = rx_at(rx);
  if (!r || !out) return false;
  ReceiverInfo i;
  i.index = rx;
  i.state = r->state.load();
  i.frequency_hz = r->applied.load();
  i.sample_rate = r->rate_now.load();
  i.effective_sps = r->sps.load();
  i.drops = r->drops.load();
  portENTER_CRITICAL(&r->gain_lock);
  std::memcpy(i.device, r->info.device, sizeof(i.device));
  std::memcpy(i.usb_path, r->info.usb_path, sizeof(i.usb_path));
  portEXIT_CRITICAL(&r->gain_lock);
  *out = i;
  return true;
}

// ---- sinks -------------------------------------------------------------------------------------------------------

SinkHandle add_sink(const SinkSpec& spec) {
  Rx* r = rx_at(spec.receiver);
  if (!r || !spec.fn) return -1;
  for (int slot = 0; slot < kMaxSinks; ++slot) {
    Sink& s = r->sinks[slot];
    int expected = 0;
    if (!s.state.compare_exchange_strong(expected, 3)) continue;
    if (!s.lock) s.lock = xSemaphoreCreateMutex();
    if (!s.lock) {
      s.state = 0;
      return -1;
    }
    s.fn = spec.fn;
    s.ctx = spec.context;
    s.kind = spec.kind;
    s.receiver = spec.receiver;
    std::snprintf(s.name, sizeof(s.name), "%s", spec.name ? spec.name : "sink");
    s.blocks = 0;
    s.drops = 0;
    s.max_us = 0;
    if (spec.kind == SinkKind::queued) {
      const size_t bytes = spec.queue_bytes ? spec.queue_bytes : 4 * kFrameBytes;
      s.mb = xMessageBufferCreateWithCaps(bytes < 2 * kFrameBytes ? 2 * kFrameBytes : bytes, MALLOC_CAP_SPIRAM);
      s.run = true;
      s.exited = false;
      if (!s.mb || xTaskCreatePinnedToCore(queued_task, s.name, spec.task_stack, &s, spec.task_priority, &s.task,
                                           spec.task_core < 0 ? tskNO_AFFINITY : spec.task_core) != pdPASS) {
        if (s.mb) vMessageBufferDeleteWithCaps(s.mb);
        s.mb = nullptr;
        s.run = false;
        s.exited = true;
        s.state = 0;
        return -1;
      }
    }
    s.state.store(1, std::memory_order_release);
    return spec.receiver * kMaxSinks + slot;
  }
  return -1;
}

bool remove_sink(SinkHandle handle) {
  if (handle < 0 || handle >= kRx * kMaxSinks) return false;
  Rx& r = g_rx[handle / kMaxSinks];
  Sink& s = r.sinks[handle % kMaxSinks];
  int expected = 1;
  if (!s.state.compare_exchange_strong(expected, 2)) return false;  // not active (already removed, or quarantined earlier)
  // State 2 stops new calls. The radio loop holds s.lock while it calls or feeds this sink and re-reads the state under the lock, so once
  // we hold the lock no call is running and none can start. (If we ARE the radio task, called from inside a sink, we are the only
  // dispatcher and are not dispatching right now, so there is nothing to wait for.)
  const bool on_radio_task = xTaskGetCurrentTaskHandle() == r.task_handle;
  if (!on_radio_task && xSemaphoreTake(s.lock, pdMS_TO_TICKS(1500)) != pdTRUE) {
    ESP_LOGE("RADIO", "sink '%s' did not finish its callback in 1.5 s: quarantined, its memory must not be freed", s.name);
    return false;  // state stays 2: the slot is never reused
  }
  bool ok = true;
  if (s.kind == SinkKind::queued) {
    s.run = false;  // the consumer's own task ends; wait for it so nothing uses the buffer after it is deleted
    for (int i = 0; i < 200 && !s.exited.load(); ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (!s.exited.load()) {
      ESP_LOGE("RADIO", "queued sink '%s' did not stop in 2 s: quarantined, its memory must not be freed", s.name);
      ok = false;
    } else {
      if (s.mb) vMessageBufferDeleteWithCaps(s.mb);
      s.mb = nullptr;
    }
  }
  if (ok) {
    s.fn = nullptr;
    s.ctx = nullptr;
  }
  if (!on_radio_task) xSemaphoreGive(s.lock);
  if (ok) s.state.store(0, std::memory_order_release);
  return ok;
}

bool sink_stats(SinkHandle handle, SinkStats* out) {
  if (handle < 0 || handle >= kRx * kMaxSinks || !out) return false;
  const Sink& s = g_rx[handle / kMaxSinks].sinks[handle % kMaxSinks];
  if (s.state.load() != 1) return false;
  SinkStats st;
  std::snprintf(st.name, sizeof(st.name), "%s", s.name);
  st.queued = s.kind == SinkKind::queued;
  st.blocks = s.blocks.load();
  st.drops = s.drops.load();
  st.max_call_us = s.max_us.load();
  *out = st;
  return true;
}

void set_iq_sink(IqSink sink) { set_legacy(0, sink); }
void set_aux_iq_sink(IqSink sink) { set_legacy(1, sink); }

// ---- per-receiver controls (the un-indexed forms act on receiver 0) ----------------------------------------------

Info info(int rx) {
  Info i;
  if (Rx* r = rx_at(rx)) {
    portENTER_CRITICAL(&r->gain_lock);
    i = r->info;
    portEXIT_CRITICAL(&r->gain_lock);
  }
  return i;
}
Info info() { return info(0); }
void set_ppm(int ppm) {
  g_rx[0].gain_req_value = ppm;
  g_rx[0].gain_req_kind = 5;
}
void set_tuner_bandwidth(uint32_t hz) {
  g_rx[0].gain_req_value = static_cast<int>(hz);
  g_rx[0].gain_req_kind = 6;
}
void set_sample_rate_override(uint32_t rate) { g_rx[0].rate_override = rate; }
Gain gain(int rx) {
  Gain g;
  if (Rx* r = rx_at(rx)) {
    portENTER_CRITICAL(&r->gain_lock);
    g = r->gain;
    portEXIT_CRITICAL(&r->gain_lock);
  }
  return g;
}
Gain gain() { return gain(0); }
void set_gain_auto(int rx) {
  if (Rx* r = rx_at(rx)) r->gain_req_kind = 1;
}
void set_gain_auto() { set_gain_auto(0); }
void set_gain_manual(int rx, int tenth_db) {
  if (Rx* r = rx_at(rx)) {
    r->gain_req_value = tenth_db;
    r->gain_req_kind = 2;
  }
}
void set_gain_manual(int tenth_db) { set_gain_manual(0, tenth_db); }
void set_rtl_agc(int rx, bool enable) {
  if (Rx* r = rx_at(rx)) {
    r->gain_req_value = enable ? 1 : 0;
    r->gain_req_kind = 3;
  }
}
void set_rtl_agc(bool enable) { set_rtl_agc(0, enable); }
void set_bias_tee(int rx, bool enable) {
  if (Rx* r = rx_at(rx)) {
    r->gain_req_value = enable ? 1 : 0;
    r->gain_req_kind = 4;
  }
}
void set_bias_tee(bool enable) { set_bias_tee(0, enable); }
void tune_to(int rx, uint32_t frequency_hz) {
  if (Rx* r = rx_at(rx)) r->tune_request = frequency_hz;
}
void tune_to(uint32_t frequency_hz) { tune_to(0, frequency_hz); }
float level_dbfs(int rx) {
  Rx* r = rx_at(rx);
  return r && r->state.load() == 2 ? r->dbfs.load() : -120.0f;
}
float level_dbfs() { return level_dbfs(0); }
uint32_t sample_rate(int rx) {
  Rx* r = rx_at(rx);
  return r ? r->rate_now.load() : 0;
}
uint32_t sample_rate() { return sample_rate(0); }
void set_sample_rate(int rx, uint32_t rate) {
  if (Rx* r = rx_at(rx)) r->rate_target = r->rate_override.load() ? r->rate_override.load() : (rate > 1000000 ? kWideRate : kRate);
}
void set_sample_rate(uint32_t rate) { set_sample_rate(0, rate); }

waveshare4b::home_v3::Snapshot snapshot() {
  Rx& r = g_rx[0];
  return {r.requested.load(), r.applied.load(), r.sps.load(), r.drops.load(), kSteps[r.step_idx.load()],
          uint32_t(esp_timer_get_time() / 1000000), r.state.load()};
}

bool take_bins(int rx, uint8_t out[256]) {
  Rx* r = rx_at(rx);
  if (!r) return false;
  bool fresh = false;
  portENTER_CRITICAL(&r->bands_lock);
  if (r->band_seq != r->taken_seq) {
    memcpy(out, r->bands, sizeof(r->bands));
    r->taken_seq = r->band_seq;
    fresh = true;
  }
  portEXIT_CRITICAL(&r->bands_lock);
  return fresh;
}
bool take_bins(uint8_t out[256]) { return take_bins(0, out); }
}  // namespace orcsdr::radio
