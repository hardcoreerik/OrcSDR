#include "am_finder.hpp"

#include "rf_analysis.hpp"

#include <esp_attr.h>
#include <esp_timer.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>

namespace orcsdr::am_finder {
namespace {

// Snapshot retains 1024 bins; two FFT bins per output preserve peak detection
// without the multi-second 8192-point PSRAM FFT latency on Tab5.
constexpr uint16_t kFftSize = 2048;
constexpr uint32_t kMinimumCaptureMs = 1500;
constexpr uint32_t kPreferredFrames = 8;
constexpr uint32_t kSlowAnalyzerMs = 3000;
constexpr uint32_t kFailureMs = 4000;
constexpr uint32_t kStartFailureMs = 8000;

std::atomic<bool> g_active{false};
uint32_t g_spacing_hz = 10000;
uint32_t g_started_ms = 0;
std::atomic<uint32_t> g_capture_started_ms{0};
uint32_t g_start_frame = 0;
uint32_t g_start_drops = 0;
uint32_t g_last_poll_ms = 0;
EXT_RAM_BSS_ATTR rf_analysis::Snapshot g_latest{};

uint32_t now_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

size_t channel_count(uint32_t spacing_hz) {
  const uint32_t start_hz = spacing_hz == 9000 ? 531000u : kMinHz;
  return (kMaxHz - start_hz) / spacing_hz + 1;
}

bool extract_grid_levels(const rf_analysis::Snapshot& snapshot, uint32_t spacing_hz,
                         float* levels, size_t capacity, size_t* count) {
  if (!levels || !count || (spacing_hz != 9000 && spacing_hz != 10000) ||
      snapshot.bins < 32 || !snapshot.span_hz) return false;
  const size_t needed = channel_count(spacing_hz);
  if (needed > capacity) return false;
  const uint32_t start_hz = spacing_hz == 9000 ? 531000u : kMinHz;
  const double low_hz = static_cast<double>(snapshot.center_hz) - snapshot.span_hz / 2.0;
  const double bin_hz = static_cast<double>(snapshot.span_hz) / snapshot.bins;
  for (size_t channel = 0; channel < needed; ++channel) {
    const double frequency_hz = start_hz + channel * static_cast<double>(spacing_hz);
    const long nearest = lround((frequency_hz - low_hz) / bin_hz);
    float strongest = -160.0f;
    for (long offset = -1; offset <= 1; ++offset) {
      const long bin = nearest + offset;
      if (bin >= 0 && bin < snapshot.bins)
        strongest = std::max(strongest, snapshot.average[bin]);
    }
    levels[channel] = strongest;
  }
  *count = needed;
  return true;
}

void stop_analysis() {
  g_active.store(false, std::memory_order_release);
  rf_analysis::set_enabled(false);
}

}  // namespace

bool start(uint32_t spacing_hz) {
  if ((spacing_hz != 9000 && spacing_hz != 10000) || !rf_analysis::initialize())
    return false;
  g_latest = {};
  if (!rf_analysis::copy_snapshot(&g_latest)) return false;
  rf_analysis::Config config{};
  config.center_hz = kCenterHz;
  config.span_hz = kSampleRateSps;
  config.sample_rate_sps = kSampleRateSps;
  config.measurement_bandwidth_hz = kMaxHz - kMinHz;
  config.fft_size = kFftSize;
  config.interval_ms = 75;
  rf_analysis::set_config(config);
  rf_analysis::clear_average();
  rf_analysis::clear_peak();
  g_spacing_hz = spacing_hz;
  g_start_frame = g_latest.frame_count;
  g_start_drops = g_latest.input_drops;
  g_started_ms = now_ms();
  g_capture_started_ms.store(0, std::memory_order_release);
  g_last_poll_ms = 0;
  g_latest = {};
  g_active.store(true, std::memory_order_release);
  rf_analysis::set_enabled(true);
  return true;
}

bool active() { return g_active.load(std::memory_order_acquire); }

void offer_iq(const uint8_t* iq, size_t bytes, uint32_t sample_rate_sps) {
  if (!active()) return;
  if (sample_rate_sps != kSampleRateSps) return;
  uint32_t not_started = 0;
  (void)g_capture_started_ms.compare_exchange_strong(
      not_started, now_ms(), std::memory_order_acq_rel);
  rf_analysis::offer_iq(iq, bytes);
}

Poll poll() {
  if (!active()) return Poll::failed;
  const uint32_t now = now_ms();
  const uint32_t capture_started = g_capture_started_ms.load(std::memory_order_acquire);
  if (!capture_started)
    return now - g_started_ms >= kStartFailureMs ? Poll::failed : Poll::collecting;
  const uint32_t elapsed = now - capture_started;
  if (elapsed < kMinimumCaptureMs || now - g_last_poll_ms < 100) return Poll::collecting;
  g_last_poll_ms = now;
  if (rf_analysis::copy_snapshot(&g_latest) &&
      g_latest.center_hz == kCenterHz && g_latest.sample_rate_sps == kSampleRateSps) {
    const uint32_t frames = g_latest.frame_count - g_start_frame;
    if (frames >= kPreferredFrames || (frames > 0 && elapsed >= kSlowAnalyzerMs))
      return Poll::ready;
  }
  return elapsed >= kFailureMs ? Poll::failed : Poll::collecting;
}

uint8_t progress_percent() {
  if (!active()) return 0;
  const uint32_t capture_started = g_capture_started_ms.load(std::memory_order_acquire);
  if (!capture_started) return 5;
  return static_cast<uint8_t>(std::min<uint32_t>(
      95, (now_ms() - capture_started) * 95u / kSlowAnalyzerMs));
}

bool finish(float* levels, size_t capacity, size_t* count, Report* report) {
  const bool copied = active() && rf_analysis::copy_snapshot(&g_latest);
  const bool valid = copied && g_latest.center_hz == kCenterHz &&
                     g_latest.sample_rate_sps == kSampleRateSps &&
                     g_latest.frame_count > g_start_frame &&
                     extract_grid_levels(g_latest, g_spacing_hz, levels, capacity, count);
  if (report) {
    report->frames = g_latest.frame_count - g_start_frame;
    report->input_drops = g_latest.input_drops - g_start_drops;
    report->clipping_percent = g_latest.clipping_percent;
  }
  stop_analysis();
  return valid;
}

void cancel() {
  if (active()) stop_analysis();
}

bool self_check() {
  if ((kCenterHz - 531000u) % 9000u == 0 || (kCenterHz - 530000u) % 10000u == 0)
    return false;
  auto& snapshot = g_latest;
  snapshot = {};
  snapshot.center_hz = kCenterHz;
  snapshot.span_hz = kSampleRateSps;
  snapshot.sample_rate_sps = kSampleRateSps;
  snapshot.bins = rf_analysis::kMaxBins;
  std::fill_n(snapshot.average, snapshot.bins, -80.0f);
  const double low_hz = snapshot.center_hz - snapshot.span_hz / 2.0;
  const double bin_hz = static_cast<double>(snapshot.span_hz) / snapshot.bins;
  const size_t hot_bin = static_cast<size_t>(lround((1280000.0 - low_hz) / bin_hz));
  snapshot.average[hot_bin] = -30.0f;
  float levels[kMaxChannels]{};
  size_t count = 0;
  if (!extract_grid_levels(snapshot, 10000, levels, std::size(levels), &count) ||
      count != 119) return false;
  const size_t hot_channel = (1280000u - kMinHz) / 10000u;
  return levels[hot_channel] == -30.0f && levels[hot_channel - 1] == -80.0f &&
         levels[hot_channel + 1] == -80.0f;
}

}  // namespace orcsdr::am_finder
