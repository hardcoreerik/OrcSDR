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
constexpr uint32_t kRate = 960000;
constexpr size_t kIqBytes = 16384;

QueueHandle_t queue = nullptr;
std::atomic<uint32_t> requested{kDefaultHz}, applied{0}, sps{0}, drops{0}, step_idx{2};
std::atomic<int> state{0};  // 0 no receiver, 1 scanning, 2 streaming, 3 error
std::atomic<uint32_t> tune_request{0};
std::atomic<float> dbfs{-120.0f};
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
  int64_t last_scan = 0, last_fft = 0, last_metrics = 0;
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
          sc.sample_rate_sps = kRate;
          const esp_err_t e = esp_rtl_sdr_start(sdr, &sc);
          if (e == ESP_OK) {
            streaming = true;
            state = 2;
            ESP_LOGI(TAG, "RF START hz=%u rate=%u", unsigned(sc.frequency_hz), unsigned(kRate));
          } else {
            ESP_LOGE(TAG, "start failed: %s", esp_rtl_sdr_err_to_name(e));
            state = 3;
          }
        } else {
          state = 0;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(30));
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
    if (e == ESP_OK && n >= 2048 && now - last_fft > 150000) {
      // Level of the block as RMS of the centred 8-bit samples, in dBFS.
      double acc = 0.0;
      const size_t count = n < 4096 ? n : 4096;
      for (size_t i = 0; i < count; ++i) {
        const double v = (static_cast<double>(iq[i]) - 127.5) / 127.5;
        acc += v * v;
      }
      const double rms = std::sqrt(acc / static_cast<double>(count));
      dbfs = rms > 1e-6 ? static_cast<float>(20.0 * std::log10(rms)) : -120.0f;
    }
    if (e == ESP_OK && n >= kFftSize * 2 && now - last_fft > 150000) {
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

void tune_to(uint32_t frequency_hz) { tune_request = frequency_hz; }
float level_dbfs() { return state.load() == 2 ? dbfs.load() : -120.0f; }
uint32_t sample_rate() { return kRate; }

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
