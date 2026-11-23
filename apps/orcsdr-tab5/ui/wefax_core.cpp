#include "wefax_core.hpp"
#include <algorithm>
#include <cmath>

namespace orcsdr::wefax {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kLpfAlpha = 0.12f;
constexpr uint32_t kWarmupSamples = 72; // 6 ms of initial discriminator history
// Down-convert at exactly 1900 Hz, taking the phase difference as the
// signed deviation from center; never interpret oscillator/sample rate as RF.
constexpr float kOscStep = -kTwoPi * kCenterHz / static_cast<float>(kDecodeRate);
const float kOscCos = std::cos(kOscStep);
const float kOscSin = std::sin(kOscStep);
} // namespace

bool valid_format(const Format& f) {
  return (f.ioc == 288 || f.ioc == 576) &&
         (f.lpm == 60 || f.lpm == 90 || f.lpm == 120 || f.lpm == 240) &&
         f.max_rows > 0;
}
uint16_t pixel_width(uint16_t ioc) {
  return ioc == 576 ? 1810 : (ioc == 288 ? 905 : 0);
}

Decoder::Decoder(Format format) {
  if (valid_format(format)) format_ = format;
  width_ = pixel_width(format_.ioc);
  reset();
}
bool Decoder::configure(const Format& format) {
  if (metrics_.receiving || !valid_format(format)) return false;
  format_ = format;
  width_ = pixel_width(format_.ioc);
  reset();
  return true;
}
void Decoder::reset() {
  metrics_ = {};
  image_sample_ = 0;
  pixel_column_ = 0;
  pixel_sum_ = 0;
  pixel_count_ = 0;
  last_pixel_ = 0;
  decimate_sum_ = 0;
  decimate_n_ = 0;
  mix_i1_ = mix_q1_ = mix_i2_ = mix_q2_ = 0.0f;
  prev_i_ = prev_q_ = 0.0f;
  oscillator_i_ = 1.0f;
  oscillator_q_ = 0.0f;
  oscillator_rebase_ = 0;
  warmup_ = 0;
}
void Decoder::set_row_sink(RowSink sink, void* context) {
  sink_ = sink;
  sink_context_ = context;
}
void Decoder::start_manual() {
  metrics_.receiving = true;
  metrics_.capped = false;
  metrics_.rows = 0;
  image_sample_ = 0;
  pixel_column_ = 0;
  pixel_sum_ = 0;
  pixel_count_ = 0;
  last_pixel_ = 0;
  row_.fill(0);
}
void Decoder::stop_manual() {
  metrics_.receiving = false;
  image_sample_ = 0;
  pixel_column_ = 0;
  pixel_sum_ = 0;
  pixel_count_ = 0;
}
void Decoder::process_pcm_12k(const int16_t* samples, size_t count) {
  if (!samples) return;
  for (size_t i = 0; i < count; ++i) feed_sample(samples[i]);
}
void Decoder::process_pcm_48k(const int16_t* samples, size_t count) {
  if (!samples) return;
  for (size_t i = 0; i < count; ++i) {
    decimate_sum_ += samples[i];
    if (++decimate_n_ == 4) {
      feed_sample(static_cast<int16_t>(decimate_sum_ / 4));
      decimate_sum_ = 0;
      decimate_n_ = 0;
    }
  }
}
void Decoder::feed_sample(int16_t raw) {
  ++metrics_.samples_12k;
  if (raw >= 32766 || raw <= -32767) ++metrics_.clipped_pcm;
  const float x = static_cast<float>(raw) / 32768.0f;
  metrics_.input_envelope += 0.001f * (std::abs(x) - metrics_.input_envelope);
  // Running envelope is informational, not a certainty of valid WEFAX.
  metrics_.signal_present = metrics_.input_envelope > 0.008f;
  if (!metrics_.signal_present) ++metrics_.silent_pcm;

  const float i = x * oscillator_i_, q = x * oscillator_q_;
  mix_i1_ += kLpfAlpha * (i - mix_i1_);
  mix_q1_ += kLpfAlpha * (q - mix_q1_);
  mix_i2_ += kLpfAlpha * (mix_i1_ - mix_i2_);
  mix_q2_ += kLpfAlpha * (mix_q1_ - mix_q2_);

  const float next_i = oscillator_i_ * kOscCos - oscillator_q_ * kOscSin;
  const float next_q = oscillator_i_ * kOscSin + oscillator_q_ * kOscCos;
  oscillator_i_ = next_i;
  oscillator_q_ = next_q;
  if (++oscillator_rebase_ == 1024) {
    oscillator_rebase_ = 0;
    const float mag = std::sqrt(oscillator_i_ * oscillator_i_ + oscillator_q_ * oscillator_q_);
    if (mag > 0.01f) { oscillator_i_ /= mag; oscillator_q_ /= mag; }
  }

  bool valid = false;
  float freq = kCenterHz;
  const float power = mix_i2_ * mix_i2_ + mix_q2_ * mix_q2_;
  if (warmup_ < kWarmupSamples) ++warmup_;
  if (warmup_ >= kWarmupSamples && power > 0.000003f &&
      metrics_.signal_present) {
    // phase(current * conj(previous))
    const float imag = mix_q2_ * prev_i_ - mix_i2_ * prev_q_;
    const float real = mix_i2_ * prev_i_ + mix_q2_ * prev_q_;
    freq = kCenterHz + std::atan2(imag, real) *
           (static_cast<float>(kDecodeRate) / kTwoPi);
    valid = std::isfinite(freq) && freq > 900.0f && freq < 2900.0f;
  }
  prev_i_ = mix_i2_;
  prev_q_ = mix_q2_;
  if (valid) metrics_.last_tone_hz = freq;
  if (metrics_.receiving) raster_sample(freq, valid);
}
void Decoder::complete_pixel() {
  uint8_t pixel = static_cast<uint8_t>(last_pixel_);
  if (pixel_count_ != 0) {
    float brightness = pixel_sum_ / static_cast<float>(pixel_count_);
    if (format_.invert) brightness = 255.0f - brightness;
    pixel = static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(255.0f, brightness))));
  }
  if (pixel_column_ < width_) row_[pixel_column_] = pixel;
  last_pixel_ = pixel;
  pixel_sum_ = 0.0f;
  pixel_count_ = 0;
}
void Decoder::complete_row() {
  if (sink_) sink_(sink_context_, metrics_.rows, row_.data(), width_);
  ++metrics_.rows;
  if (metrics_.rows >= format_.max_rows) {
    metrics_.receiving = false;
    metrics_.capped = true;
  }
}
void Decoder::raster_sample(float freq, bool valid) {
  // 60 * 12000 == 720000 is divisible by all accepted LPMs, so the
  // line length in source samples is EXACT (no long-running drift).
  const uint32_t samples_per_row = (60 * kDecodeRate) / format_.lpm;
  const uint32_t position = static_cast<uint32_t>(image_sample_ % samples_per_row);
  const uint32_t col = static_cast<uint32_t>(
      (static_cast<uint64_t>(position) * width_) / samples_per_row);
  while (pixel_column_ < col && pixel_column_ < width_) {
    complete_pixel();
    ++pixel_column_;
  }
  if (valid) {
    const float b = std::max(0.0f, std::min(255.0f,
                    (freq - kBlackHz) * (255.0f / (kWhiteHz - kBlackHz))));
    pixel_sum_ += b;
    ++pixel_count_;
  }
  ++image_sample_;
  if (position + 1 == samples_per_row) {
    while (pixel_column_ < width_) {
      complete_pixel();
      ++pixel_column_;
    }
    complete_row();
    pixel_column_ = 0;
    pixel_sum_ = 0;
    pixel_count_ = 0;
    last_pixel_ = 0;
    row_.fill(0);
  }
}
} // namespace orcsdr::wefax
