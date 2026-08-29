#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::rf_analysis {

constexpr size_t kMaxBins = 1024;
constexpr size_t kMaxIqPoints = 1024;

enum class Window : uint8_t { hann };
enum class AudioDemod : uint8_t { none, fm, am };

struct Config {
  uint32_t center_hz = 0;
  uint32_t span_hz = 960000;
  uint32_t sample_rate_sps = 960000;
  uint32_t audio_rate_sps = 48000;
  uint32_t measurement_bandwidth_hz = 0;
  uint16_t fft_size = 1024;
  uint16_t audio_fft_size = 1024;
  uint16_t dc_guard_bins = 3;
  uint16_t interval_ms = 33;
  Window window = Window::hann;
  AudioDemod audio_demod = AudioDemod::none;
  bool audio_spectrum = false;
};

struct Snapshot {
  uint32_t revision = 0;
  uint32_t analyzed_ms = 0;
  uint32_t frame_count = 0;
  uint32_t input_drops = 0;
  uint32_t center_hz = 0;
  uint32_t span_hz = 0;
  uint32_t sample_rate_sps = 0;
  uint32_t strongest_frequency_hz = 0;
  uint32_t occupied_bandwidth_hz = 0;
  uint16_t bins = 0;
  uint16_t fft_size = 0;
  uint16_t iq_count = 0;
  uint16_t audio_bins = 0;
  Window window = Window::hann;
  float rbw_hz = 0;
  float live[kMaxBins]{};
  float average[kMaxBins]{};
  float peak[kMaxBins]{};
  float noise = -160;
  float strongest = -160;
  float snr_db = 0;
  float channel_power_dbfs = -160;
  float strongest_offset_hz = 0;
  float clipping_percent = 0;
  float dc_i = 0;
  float dc_q = 0;
  float iq_imbalance_db = 0;
  bool source_stable = false;
  float iq_i[kMaxIqPoints]{};
  float iq_q[kMaxIqPoints]{};
  float audio[kMaxBins]{};
};

using Observer = void (*)(const Snapshot& snapshot, const uint8_t* iq, size_t bytes);

bool initialize_fft();
bool initialize();
void set_enabled(bool enabled);
bool enabled();
void set_config(const Config& config);
void set_observer(Observer observer);
void offer_iq(const uint8_t* iq, size_t bytes);
void offer_audio(const int16_t* left, const int16_t* right, size_t frames,
                 uint32_t sample_rate_sps);
bool copy_snapshot(Snapshot* output);
void clear_peak();
void clear_average();
bool self_check();

}  // namespace orcsdr::rf_analysis
