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
#include "freertos/task.h"
#include "home_4b_layout.hpp"
#include "home_fft.hpp"

using namespace orcsdr::waveshare4b;

namespace orcsdr::radio {
namespace {
constexpr char TAG[] = "RADIO";
constexpr uint32_t kRate = 960000;      // default rate
constexpr uint32_t kWideRate = 2400000;  // Home's wide spans
constexpr size_t kIqBytes = 16384;

QueueHandle_t queue = nullptr;
std::atomic<uint32_t> requested{kDefaultHz}, applied{0}, sps{0}, drops{0}, step_idx{2};
std::atomic<int> state{0};  // 0 no receiver, 1 scanning, 2 streaming, 3 error
std::atomic<uint32_t> tune_request{0};
std::atomic<uint32_t> rate_target{kRate}, rate_now{kRate};
IqSink g_sink = nullptr;
IqSink g_aux_sink = nullptr;
Info g_info;
std::atomic<uint32_t> rate_override{0};
std::atomic<float> dbfs{-120.0f};
Gain g_gain;                                   // cache, guarded by bands_lock's sibling below
portMUX_TYPE gain_lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<int> gain_req_kind{0};             // 0 none, 1 auto, 2 manual, 3 rtl agc, 4 bias
std::atomic<int> gain_req_value{0};
portMUX_TYPE bands_lock = portMUX_INITIALIZER_UNLOCKED;
uint8_t bands[kFftSize]{};
uint32_t band_seq = 0, taken_seq = 0;

void task(void*) {
  auto* iq = static_cast<uint8_t*>(heap_caps_malloc(kIqBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!iq) {
    state = 3;
    ESP_LOGE(TAG, "IQ buffer alloc failed");
    vTaskDelete(nullptr);
    return;
  }
  esp_rtl_sdr_config_t cfg;
  esp_rtl_sdr_config_default(&cfg);
  cfg.host_library_already_installed = false;  // one USB host owner: esp-rtl-sdr
  cfg.delivery_mode = ESP_RTL_SDR_DELIVERY_READ;
  cfg.transfer_count = 6;
  cfg.transfer_bytes = kIqBytes;
  esp_rtl_sdr_handle_t sdr = nullptr;
  const esp_err_t install = esp_rtl_sdr_install(&cfg, &sdr);
  if (install != ESP_OK) {
    ESP_LOGE(TAG, "SDR install failed: %s", esp_rtl_sdr_err_to_name(install));
    state = 3;
    heap_caps_free(iq);
    vTaskDelete(nullptr);
    return;
  }
  bool streaming = false;
  int64_t last_scan = 0, last_fft = 0, last_metrics = 0, last_stream_start_us = 0;
  for (;;) {
    const uint32_t absolute = tune_request.exchange(0);
    if (absolute) {
      requested = absolute;
      ESP_LOGI(TAG, "TUNE requested_hz=%u streaming=%d", unsigned(absolute), int(streaming));
      if (streaming) {
        const esp_err_t e = esp_rtl_sdr_retune_hz(sdr, absolute);
        if (e != ESP_OK) ESP_LOGE(TAG, "retune failed: %s", esp_rtl_sdr_err_to_name(e));
      }
    }
    home_v3::Command cmd;
    while (xQueueReceive(queue, &cmd, 0) == pdTRUE) {
      if (cmd == home_v3::Command::Step) {
        step_idx = (step_idx.load() + 1) % 6;
        continue;
      }
      uint32_t next = requested.load();
      switch (cmd) {
        case home_v3::Command::Down: next = tune(next, -1, kSteps[step_idx.load()]); break;
        case home_v3::Command::Up: next = tune(next, 1, kSteps[step_idx.load()]); break;
        case home_v3::Command::FM: next = 96100000; break;
        case home_v3::Command::WX: next = 162400000; break;
        case home_v3::Command::Air: next = 125000000; break;
        case home_v3::Command::Ham: next = 145120000; break;
        default: break;
      }
      requested = next;
      ESP_LOGI(TAG, "TUNE requested_hz=%u streaming=%d", unsigned(next), int(streaming));
      if (streaming) {
        const esp_err_t e = esp_rtl_sdr_retune_hz(sdr, next);
        if (e != ESP_OK) ESP_LOGE(TAG, "retune failed: %s", esp_rtl_sdr_err_to_name(e));
      }
    }
    const int64_t now = esp_timer_get_time();
    if (!streaming) {
      applied = 0;
      sps = 0;
      if (now - last_scan > 1000000) {
        last_scan = now;
        state = 1;
        size_t count = 0, raw_count = 0;
        const esp_err_t raw_result = esp_rtl_sdr_usb_device_count(&raw_count);
        const esp_err_t refresh_result = esp_rtl_sdr_refresh_device_list(sdr);
        const esp_err_t count_result =
            refresh_result == ESP_OK ? esp_rtl_sdr_get_device_count(sdr, &count) : refresh_result;
        // The raw USB count includes hubs and unsupported devices; an empty candidate list
        // is not by itself evidence that the board supplies VBUS.
        static uint32_t scans = 0;
        if ((++scans % 5u) == 1u || raw_count || count) {
          ESP_LOGI(TAG, "USB_DIAG raw_devices=%u accepted_sdr=%u raw=%s scan=%s safe_mode=%u",
                   unsigned(raw_count), unsigned(count), esp_rtl_sdr_err_to_name(raw_result),
                   esp_rtl_sdr_err_to_name(count_result), unsigned(esp_rtl_sdr_usb_safe_mode_active()));
        }
        if (refresh_result == ESP_OK && count_result == ESP_OK && count > 0) {
          esp_rtl_sdr_stream_config_t sc;
          esp_rtl_sdr_stream_config_default(&sc);
          sc.preset = ESP_RTL_SDR_PRESET_CUSTOM_HZ;
          sc.frequency_hz = requested.load();
          sc.sample_rate_sps = rate_target.load();
          const esp_err_t e = esp_rtl_sdr_start(sdr, &sc);
          if (e == ESP_OK) {
            streaming = true;
            state = 2;
            last_stream_start_us = now;
            rate_now = sc.sample_rate_sps;
            ESP_LOGI(TAG, "RF START hz=%u rate=%u", unsigned(sc.frequency_hz), unsigned(sc.sample_rate_sps));
          } else {
            ESP_LOGE(TAG, "start failed: %s", esp_rtl_sdr_err_to_name(e));
            state = 3;
            // A failed start can leave the driver in FAULT; clear it (stop, then reset) so the next scan can retry.
            (void)esp_rtl_sdr_stop(sdr, 1000);
            const esp_err_t r = esp_rtl_sdr_reset(sdr);
            ESP_LOGW(TAG, "driver reset after failed start: %s (state %s)", esp_err_to_name(r),
                     esp_rtl_sdr_state_to_name(esp_rtl_sdr_get_state(sdr)));
          }
        } else {
          state = 0;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    // A rate change is a USB stop/start. Wait until the requested rate has been steady for a moment (screens often ask
    // twice in quick succession) and never restart twice within 1.5 s: back-to-back restarts left the USB device stuck.
    static uint32_t last_seen_target = 0;
    static int64_t target_changed_at = 0;
    const uint32_t wanted_rate = rate_target.load();
    if (wanted_rate != last_seen_target) {
      last_seen_target = wanted_rate;
      target_changed_at = now;
    }
    if (wanted_rate != rate_now.load() && now - target_changed_at > 400000 && now - last_stream_start_us > 1500000) {
      // The driver changes sample rate only across stop/start.
      ESP_LOGI(TAG, "sample rate %u -> %u", unsigned(rate_now.load()), unsigned(rate_target.load()));
      (void)esp_rtl_sdr_stop(sdr, 1500);
      streaming = false;
      state = 1;
      last_scan = 0;
      continue;
    }
    if (esp_rtl_sdr_get_state(sdr) != ESP_RTL_SDR_STATE_STREAMING) {
      ESP_LOGW(TAG, "stream ended, rescanning");
      (void)esp_rtl_sdr_stop(sdr, 1500);
      streaming = false;
      state = 0;
      continue;
    }
    size_t n = 0;
    const esp_err_t e = esp_rtl_sdr_read(sdr, iq, kIqBytes, 40, &n);
    if (e == ESP_OK && n && g_sink) g_sink(iq, n);
    if (e == ESP_OK && n && g_aux_sink) g_aux_sink(iq, n);
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
      dbfs = rms > 1e-6f ? 20.0f * std::log10(rms) : -120.0f;
    }
    if (e == ESP_OK && n >= kFftSize * 2 && now - last_fft > 50000) {
      Spectrum view = spectrum_from_cu8(iq, n);
      if (view.real_samples) {
        portENTER_CRITICAL(&bands_lock);
        memcpy(bands, view.values.data(), kFftSize);
        ++band_seq;
        portEXIT_CRITICAL(&bands_lock);
        last_fft = now;
      }
    } else if (e != ESP_OK && e != ESP_RTL_SDR_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "read error %s", esp_rtl_sdr_err_to_name(e));
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    {
      // Apply a queued control change, then refresh the cache (about twice a second).
      static int64_t last_gain_poll = 0;
      const int kind = gain_req_kind.exchange(0);
      if (kind) {
        const int value = gain_req_value.load();
        esp_err_t r = ESP_OK;
        if (kind == 1) r = esp_rtl_sdr_set_tuner_gain_mode(sdr, ESP_RTL_SDR_GAIN_MODE_AUTO);
        else if (kind == 2) {
          r = esp_rtl_sdr_set_tuner_gain_mode(sdr, ESP_RTL_SDR_GAIN_MODE_MANUAL);
          if (r == ESP_OK) r = esp_rtl_sdr_set_tuner_gain(sdr, value);
        } else if (kind == 3) r = esp_rtl_sdr_set_rtl_agc(sdr, value != 0);
        else if (kind == 4) r = esp_rtl_sdr_set_bias_tee(sdr, value != 0);
        else if (kind == 5) r = esp_rtl_sdr_set_freq_correction(sdr, value);
        else if (kind == 6) r = esp_rtl_sdr_set_tuner_bandwidth(sdr, static_cast<uint32_t>(value));
        ESP_LOGI(TAG, "GAIN request kind=%d value=%d -> %s", kind, value, esp_rtl_sdr_err_to_name(r));
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
        esp_rtl_sdr_health_info_t hi{};
        if (esp_rtl_sdr_get_health(sdr, &hi) == ESP_OK)
          std::snprintf(inf.health, sizeof(inf.health), "%.27s", hi.advice[0] ? hi.advice : "OK");
        else
          std::snprintf(inf.health, sizeof(inf.health), "unknown");
        std::snprintf(inf.version, sizeof(inf.version), "%s", esp_rtl_sdr_get_version_string());
        portENTER_CRITICAL(&gain_lock);
        g_info = inf;
        g_gain = g;
        portEXIT_CRITICAL(&gain_lock);
      }
    }
    if (now - last_metrics > 750000) {
      last_metrics = now;
      esp_rtl_sdr_metrics_t m{};
      if (esp_rtl_sdr_get_metrics(sdr, &m) == ESP_OK) {
        applied = m.frequency_hz;
        sps = m.effective_sps;
        drops = m.slot_starve_blocks + m.ring_overrun_blocks + m.pull_lock_miss_blocks;
        ESP_LOGI(TAG, "IQ hz=%u sps=%u consumed=%llu drops=%u", unsigned(m.frequency_hz),
                 unsigned(m.effective_sps), (unsigned long long)m.bytes_consumed, unsigned(drops.load()));
      }
    }
  }
}
}  // namespace

bool start(QueueHandle_t commands) {
  queue = commands;
  ESP_LOGI(TAG, "driver=%s psram_free=%u", esp_rtl_sdr_get_version_string(),
           unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  return xTaskCreatePinnedToCore(task, "radio", 8192, nullptr, 5, nullptr, 1) == pdPASS;
}

void set_iq_sink(IqSink sink) { g_sink = sink; }
void set_aux_iq_sink(IqSink sink) { g_aux_sink = sink; }
Info info() {
  Info i;
  portENTER_CRITICAL(&gain_lock);
  i = g_info;
  portEXIT_CRITICAL(&gain_lock);
  return i;
}
void set_ppm(int ppm) {
  gain_req_value = ppm;
  gain_req_kind = 5;
}
void set_tuner_bandwidth(uint32_t hz) {
  gain_req_value = static_cast<int>(hz);
  gain_req_kind = 6;
}
void set_sample_rate_override(uint32_t rate) { rate_override = rate; }
Gain gain() {
  Gain g;
  portENTER_CRITICAL(&gain_lock);
  g = g_gain;
  portEXIT_CRITICAL(&gain_lock);
  return g;
}
void set_gain_auto() { gain_req_kind = 1; }
void set_gain_manual(int tenth_db) {
  gain_req_value = tenth_db;
  gain_req_kind = 2;
}
void set_rtl_agc(bool enable) {
  gain_req_value = enable ? 1 : 0;
  gain_req_kind = 3;
}
void set_bias_tee(bool enable) {
  gain_req_value = enable ? 1 : 0;
  gain_req_kind = 4;
}
void tune_to(uint32_t frequency_hz) { tune_request = frequency_hz; }
float level_dbfs() { return state.load() == 2 ? dbfs.load() : -120.0f; }
uint32_t sample_rate() { return rate_now.load(); }
void set_sample_rate(uint32_t rate) { rate_target = rate_override.load() ? rate_override.load() : (rate > 1000000 ? kWideRate : kRate); }

waveshare4b::home_v3::Snapshot snapshot() {
  return {requested.load(), applied.load(), sps.load(), drops.load(), kSteps[step_idx.load()],
          uint32_t(esp_timer_get_time() / 1000000), state.load()};
}

bool take_bins(uint8_t out[256]) {
  bool fresh = false;
  portENTER_CRITICAL(&bands_lock);
  if (band_seq != taken_seq) {
    memcpy(out, bands, sizeof(bands));
    taken_seq = band_seq;
    fresh = true;
  }
  portEXIT_CRITICAL(&bands_lock);
  return fresh;
}
}  // namespace orcsdr::radio
