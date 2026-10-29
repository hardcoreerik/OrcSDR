#pragma once
// OrcSDR WEFAX phase-1 receiver core. Standalone C++17, no ESP-IDF, display,
// storage, or heap allocation. A radio/audio owner must provide one PCM rate.
// This is MANUAL-framed reception only: no automatic APT or phasing lock yet.
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::wefax {
constexpr uint32_t kDecodeRate = 12000;
constexpr uint16_t kMaxPixelsPerLine = 1810;
constexpr float kBlackHz = 1500.0f;
constexpr float kWhiteHz = 2300.0f;
constexpr float kCenterHz = 1900.0f;

struct Format {
  uint16_t ioc = 576; // 288 or 576 (NOT an output pixel count).
  uint16_t lpm = 120; // 60, 90, 120, or 240 lines/min.
  uint16_t max_rows = 2400; // bounded output, never wraps into the next page.
  bool invert = false;
};

struct Metrics {
  uint64_t samples_12k = 0; // valid-rate samples consumed, including before manual start
  uint32_t rows = 0;
  uint32_t clipped_pcm = 0;
  uint32_t silent_pcm = 0;
  float last_tone_hz = 0.0f; // untrusted when signal_present=false
  float input_envelope = 0.0f;
  bool signal_present = false;
  bool receiving = false;
  bool capped = false;
};

// Row pointer remains valid until the next process call; copy or consume it in
// the callback. Callbacks must be bounded and must not call Decoder methods.
using RowSink = void (*)(void* context, uint32_t row, const uint8_t* pixels,
                         uint16_t width);

bool valid_format(const Format& format);
uint16_t pixel_width(uint16_t ioc); // round(pi * IOC): 1810 / 905.

class Decoder {
 public:
  explicit Decoder(Format format = {});
  // Reconfigure only while not receiving. Invalid input is rejected unchanged.
  bool configure(const Format& format);
  void reset(); // stop, clear DSP state, preserve format and row callback
  void set_row_sink(RowSink sink, void* context);
  // Aligns scan-line start to the NEXT sample. Caller must know the image
  // boundary; use a phasing reference or manual tuning, not an assumed RF lock.
  void start_manual();
  void stop_manual(); // keeps any already-emitted rows; drops partial row
  // Both methods accept arbitrarily sized chunks; do not interleave them.
  // 48k uses continuous 4:1 averaging before the 12k discriminator.
  void process_pcm_12k(const int16_t* samples, size_t count);
  void process_pcm_48k(const int16_t* samples, size_t count);
  const Format& format() const { return format_; }
  const Metrics& metrics() const { return metrics_; }

 private:
  void feed_sample(int16_t sample);
  void raster_sample(float tone_hz, bool valid);
  void complete_pixel();
  void complete_row();

  Format format_{};
  Metrics metrics_{};
  RowSink sink_ = nullptr;
  void* sink_context_ = nullptr;
  std::array<uint8_t, kMaxPixelsPerLine> row_{};
  uint16_t width_ = 0;
  // Integer rational line/pixel timing avoids cumulative floating-point drift.
  uint64_t image_sample_ = 0;
  uint32_t pixel_column_ = 0;
  float pixel_sum_ = 0.0f;
  uint32_t pixel_count_ = 0;
  uint16_t last_pixel_ = 0;
  int32_t decimate_sum_ = 0;
  uint8_t decimate_n_ = 0;
  // Two low-pass stages remove the ~3.8 kHz mixer image; this is intentionally
  // fixed/filter-limited for phase 1, not a claimed optimal adaptive filter.
  float mix_i1_ = 0.0f, mix_q1_ = 0.0f;
  float mix_i2_ = 0.0f, mix_q2_ = 0.0f;
  float prev_i_ = 0.0f, prev_q_ = 0.0f;
  float oscillator_i_ = 1.0f, oscillator_q_ = 0.0f;
  uint32_t oscillator_rebase_ = 0;
  uint32_t warmup_ = 0;
};
}  // namespace orcsdr::wefax
