// PlutoSDR (AD9361) as an OrcSDR receiver source, over the board's USB host port. This file is only the adapter: the device logic (USB, iiod,
// rate and decimation planning, ranges, gain, the rate-change barrier, metrics) lives in the esp-pluto-sdr driver (pluto_sdr.h), which does not
// depend on OrcSDR. The adapter claims receiver 0 through radio::ext while a Pluto is attached and no RTL-SDR is streaming there, forwards the
// console's tune / rate / gain requests to the driver, and feeds the driver's blocks to the receiver's sinks, so audio, RF Lab, the decoders and
// the browser stream need no change. Receive only: the driver never writes a transmit attribute.
#include "pluto_service.hpp"

#include "sdkconfig.h"

#if CONFIG_ORCSDR_PLUTO_SOURCE

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pluto_sdr.h"
#include "radio_service.hpp"

namespace orcsdr::pluto {
namespace {
const char* TAG = "PLUTO";
constexpr int kRx = 0;  // the receiver this source takes (the RTL-SDR keeps it whenever it is streaming)
pluto_sdr_handle_t g_handle = nullptr;  // for snapshot() and the rate quantizer
bool quantize(uint32_t wanted, uint32_t* applied) { return g_handle && pluto_sdr_quantize_sample_rate(g_handle, wanted, applied) == PLUTO_SDR_OK; }

uint32_t clamp32(uint64_t v) { return v > 0xFFFFFFFFULL ? 0xFFFFFFFFu : static_cast<uint32_t>(v); }

void publish(pluto_sdr_handle_t h, int state, uint32_t sps, uint32_t drops) {
  pluto_sdr_caps_t caps;
  pluto_sdr_info_t info;
  pluto_sdr_gain_t pg;
  pluto_sdr_get_capabilities(h, &caps);
  pluto_sdr_get_device_info(h, &info);
  pluto_sdr_get_gain(h, &pg);
  radio::ext::Status st;
  st.state = state;
  st.applied_hz = clamp32(pluto_sdr_get_center_freq(h));
  st.rate = pluto_sdr_get_sample_rate(h);
  st.sps = sps;
  st.drops = drops;
  if (caps.valid) {
    st.tune_min_hz = clamp32(caps.tune_min_hz);
    // The console's frequency type is 32 bits, so tuning above 4.29 GHz is not reachable until it becomes 64 bits (the device goes to 6 GHz).
    st.tune_max_hz = clamp32(caps.tune_max_hz);
  }
  std::snprintf(st.device, sizeof(st.device), "%s", info.product[0] ? info.product : "PlutoSDR AD9361");
  std::snprintf(st.usb_path, sizeof(st.usb_path), "pluto-usb");
  radio::Gain g;
  g.available = caps.valid && caps.gain_max_db10 > caps.gain_min_db10;
  g.auto_available = caps.gain_auto;  // the AD9361's own AGC (slow attack), which adapts to antenna and band
  g.auto_mode = !pg.manual;
  g.tenth_db = pg.gain_db10;
  g.step_count = std::min<uint8_t>(pg.step_count, 32);
  for (int i = 0; i < g.step_count; ++i) g.steps[i] = pg.steps_db10[i];
  radio::ext::publish(kRx, st, g);
}

void forward_requests(pluto_sdr_handle_t h, uint32_t* last_rate, uint32_t* last_tune_result_hz, int* last_tune_result_err) {
  const radio::ext::Requests q = radio::ext::poll(kRx);
  if (q.tune_hz) {
    const int e = pluto_sdr_set_center_freq(h, q.tune_hz);
    if (e != PLUTO_SDR_OK) radio::ext::tune_result(kRx, q.tune_hz, e);  // refused before it reached the device (outside the reported span)
  }
  if (q.rate_target && q.rate_target != *last_rate) {
    if (pluto_sdr_set_sample_rate(h, q.rate_target) == PLUTO_SDR_OK) *last_rate = q.rate_target;
  }
  if (q.gain_kind == 1) pluto_sdr_set_gain_auto(h);
  else if (q.gain_kind == 2) pluto_sdr_set_gain_manual(h, q.gain_value);
  else if (q.gain_kind == 5) pluto_sdr_set_freq_correction(h, q.gain_value * 1000);  // whole ppm from the console's ppm request
  else if (q.gain_kind == 7) pluto_sdr_set_freq_correction(h, q.gain_value);        // thousandths of a ppm
  else if (q.gain_kind == 6) pluto_sdr_set_rf_bandwidth(h, static_cast<uint32_t>(q.gain_value));  // 0 = automatic
  // The device's own answer to the last retune goes back to the console, once per change.
  uint64_t hz = 0;
  int err = 0;
  pluto_sdr_get_tune_result(h, &hz, &err);
  if (hz && (clamp32(hz) != *last_tune_result_hz || err != *last_tune_result_err)) {
    *last_tune_result_hz = clamp32(hz);
    *last_tune_result_err = err;
    radio::ext::tune_result(kRx, clamp32(hz), err);
  }
}

// One ownership of receiver 0: forward the console's requests and feed blocks until the stream ends.
void run_session(pluto_sdr_handle_t h, uint8_t* buf, size_t cap) {
  uint32_t last_rate = 0, tr_hz = 0;
  int tr_err = 0;
  const radio::ext::Requests first = radio::ext::poll(kRx);  // takes any tune the console already asked for
  last_rate = first.rate_target ? first.rate_target : 960000;
  pluto_sdr_set_sample_rate(h, last_rate);
  pluto_sdr_set_center_freq(h, first.tune_hz ? first.tune_hz : radio::snapshot().requested_hz);
  radio::ext::new_stream(kRx);
  pluto_sdr_start(h);

  int64_t last_pub = 0, window_start = esp_timer_get_time(), not_streaming_since = 0;
  uint64_t win_feed_us = 0, win_blocks = 0;
  uint32_t log_count = 0;
  pluto_sdr_metrics_t m0{};
  pluto_sdr_get_metrics(h, &m0);
  uint32_t delivered_sps = 0;
  for (;;) {
    const int64_t now = esp_timer_get_time();
    forward_requests(h, &last_rate, &tr_hz, &tr_err);
    pluto_sdr_block_t meta{};
    const int n = pluto_sdr_read(h, buf, cap, 100, &meta);
    if (n > 0) {
      const int64_t t0 = esp_timer_get_time();
      radio::ext::feed(kRx, buf, static_cast<size_t>(n), meta.sample_rate, clamp32(meta.center_hz));
      win_feed_us += static_cast<uint64_t>(esp_timer_get_time() - t0);
      ++win_blocks;
    }
    const pluto_sdr_state_t st = pluto_sdr_get_state(h);
    // A session that ends (unplug, USB error) leaves the driver retrying on its own; give it a few seconds, then hand the receiver back.
    if (st == PLUTO_SDR_STATE_STREAMING || st == PLUTO_SDR_STATE_OPENING) {
      not_streaming_since = 0;
    } else {
      if (!not_streaming_since) not_streaming_since = now;
      if (now - not_streaming_since > 3000000) break;
    }
    if (now - window_start >= 750000) {
      pluto_sdr_metrics_t m{};
      pluto_sdr_get_metrics(h, &m);
      const uint64_t us = static_cast<uint64_t>(now - window_start);
      delivered_sps = static_cast<uint32_t>((m.samples_delivered - m0.samples_delivered) * 1000000ULL / us);
      // received = off the USB pipe; delivered = handed to the sinks; source_drops = blocks lost for want of a free buffer; stale = blocks
      // discarded by the rate-change barrier. Consumer-side drops are per sink (radio::sink_stats), audio underruns are audio's.
      ESP_LOGI(TAG, "IQ hz=%u rate=%u received_sps=%u delivered_sps=%u source_drops=%u stale=%u usb_errors=%u feed_us/block=%u",
               unsigned(clamp32(pluto_sdr_get_center_freq(h))), unsigned(pluto_sdr_get_sample_rate(h)), unsigned(m.effective_in_sps),
               unsigned(delivered_sps), unsigned(m.source_drops), unsigned(m.stale_blocks), unsigned(m.usb_errors),
               unsigned(win_blocks ? win_feed_us / win_blocks : 0));
      m0 = m;
      window_start = now;
      win_feed_us = 0;
      win_blocks = 0;
      if (++log_count % 4 == 0) {  // a coarse spectrum read: strongest bin against the median shows a real carrier from noise
        uint8_t bins[256];
        uint32_t seq = 0;
        if (radio::peek_bins(kRx, bins, &seq)) {
          int peak = 0;
          uint8_t sorted[256];
          std::memcpy(sorted, bins, sizeof(sorted));
          for (int i = 1; i < 256; ++i)
            if (bins[i] > bins[peak]) peak = i;
          std::nth_element(sorted, sorted + 128, sorted + 256);
          ESP_LOGI(TAG, "SPECTRUM peak_bin=%d (offset %+d kHz) peak=%u median=%u level=%.1f dBFS", peak,
                   static_cast<int>((peak - 128) * static_cast<int>(pluto_sdr_get_sample_rate(h)) / 256 / 1000), unsigned(bins[peak]),
                   unsigned(sorted[128]), static_cast<double>(radio::level_dbfs(kRx)));
        }
      }
    }
    if (now - last_pub > 250000) {
      last_pub = now;
      publish(h, st == PLUTO_SDR_STATE_STREAMING ? 2 : 1, delivered_sps, 0);
    }
  }
}

void task(void*) {
  while (!radio::ext::host_ready()) vTaskDelay(pdMS_TO_TICKS(500));
  pluto_sdr_config_t cfg;
  pluto_sdr_config_default(&cfg);
  cfg.install_usb_host = false;  // the RTL-SDR driver owns the USB host library; the Pluto driver adds one more client
  cfg.format = PLUTO_SDR_FORMAT_CU8;
  pluto_sdr_handle_t h = nullptr;
  if (pluto_sdr_install(&cfg, &h) != PLUTO_SDR_OK) {
    ESP_LOGE(TAG, "Pluto driver install failed");
    vTaskDelete(nullptr);
    return;
  }
  const size_t cap = pluto_sdr_max_block_bytes(h);
  auto* buf = static_cast<uint8_t*>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    ESP_LOGE(TAG, "IQ buffer alloc failed");
    pluto_sdr_uninstall(h);
    vTaskDelete(nullptr);
    return;
  }
  g_handle = h;
  radio::ext::set_rate_quantizer(kRx, quantize);
  ESP_LOGI(TAG, "PlutoSDR source ready (esp-pluto-sdr %s, receive only)", pluto_sdr_get_version_string());
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!pluto_sdr_device_present(h)) continue;
    if (!radio::ext::acquire(kRx)) {  // an RTL-SDR is streaming on the receiver: it keeps it
      ESP_LOGI(TAG, "Pluto attached but receiver %d is streaming an RTL-SDR; leaving it", kRx);
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    run_session(h, buf, cap);
    pluto_sdr_stop(h, 3000);
    radio::ext::release(kRx);
    ESP_LOGW(TAG, "PlutoSDR session ended; looking for it again");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
}  // namespace

bool start() { return xTaskCreatePinnedToCore(task, "pluto", 8192, nullptr, 5, nullptr, 0) == pdPASS; }

Snapshot snapshot() {
  Snapshot s;
  s.compiled = true;
  std::snprintf(s.version, sizeof(s.version), "%s", pluto_sdr_get_version_string());
  pluto_sdr_handle_t h = g_handle;
  if (!h) {
    std::snprintf(s.state, sizeof(s.state), "NOT_STARTED");
    return s;
  }
  std::snprintf(s.state, sizeof(s.state), "%s", pluto_sdr_state_to_name(pluto_sdr_get_state(h)));
  s.present = pluto_sdr_device_present(h);
  pluto_sdr_info_t info;
  pluto_sdr_caps_t c;
  pluto_sdr_controls_t k;
  pluto_sdr_metrics_t m;
  pluto_sdr_get_device_info(h, &info);
  pluto_sdr_get_capabilities(h, &c);
  pluto_sdr_get_controls(h, &k);
  pluto_sdr_get_metrics(h, &m);
  std::snprintf(s.device, sizeof(s.device), "%s", info.product);
  std::snprintf(s.serial, sizeof(s.serial), "%s", info.serial);
  s.high_speed = info.high_speed;
  s.caps_valid = c.valid;
  s.hardware_tx = c.hardware_tx;
  s.tx_enabled = c.tx_enabled;
  s.tune_min_hz = c.tune_min_hz;
  s.tune_max_hz = c.tune_max_hz;
  s.rate_min_sps = c.rate_min_sps;
  s.rate_max_sps = c.rate_max_sps;
  s.rate_link_max_sps = c.rate_link_max_sps;
  s.bw_min_hz = c.bw_min_hz;
  s.bw_max_hz = c.bw_max_hz;
  s.gain_min_db10 = c.gain_min_db10;
  s.gain_max_db10 = c.gain_max_db10;
  s.ppm_min_milli = c.ppm_min_milli;
  s.ppm_max_milli = c.ppm_max_milli;
  s.adc_bits = c.adc_bits;
  s.rx_channels = c.rx_channels;
  s.controls_valid = k.valid;
  s.center_requested_hz = k.center_requested_hz;
  s.center_actual_hz = k.center_actual_hz;
  s.center_error_hz = k.center_error_hz;
  s.rate_requested = k.rate_requested_sps;
  s.rate_out = k.rate_out_sps;
  s.rate_converter = k.rate_converter_sps;
  s.decimation = k.decimation;
  s.bandwidth_hz = k.bandwidth_hz;
  s.gain_manual = k.gain_manual;
  s.gain_db10 = k.gain_db10;
  s.rssi_db10 = k.rssi_db10;
  s.ppm_milli = k.ppm_milli;
  s.xo_hz = k.xo_hz;
  s.dc_rf = k.dc_tracking_rf;
  s.dc_bb = k.dc_tracking_bb;
  s.quad = k.quadrature_tracking;
  s.samples_received = m.samples_received;
  s.samples_delivered = m.samples_delivered;
  s.source_drops = m.source_drops;
  s.stale_blocks = m.stale_blocks;
  s.usb_errors = m.usb_errors;
  s.sessions = m.sessions;
  s.in_sps = m.effective_in_sps;
  s.out_sps = m.effective_out_sps;
  return s;
}
}  // namespace orcsdr::pluto

#else  // !CONFIG_ORCSDR_PLUTO_SOURCE

namespace orcsdr::pluto {
bool start() { return true; }
Snapshot snapshot() { return Snapshot{}; }
}  // namespace orcsdr::pluto

#endif
