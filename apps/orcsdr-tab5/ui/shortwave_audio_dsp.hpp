#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::shortwave::audio_dsp {

enum class NoiseReduction : uint8_t { off, low, high };
enum class SquelchMode : uint8_t { off, automatic, manual };

struct Settings {
  NoiseReduction noise_reduction = NoiseReduction::off;
  bool auto_notch = false;
  SquelchMode squelch = SquelchMode::off;
  int8_t squelch_dbfs = -65;
};

struct Metrics {
  bool squelch_open = true;
  bool notch_active = false;
  uint16_t notch_hz = 0;
  float input_rms_dbfs = -120.0f;
};

Settings settings();
Metrics metrics();
void reset();
void cycle_noise_reduction();
void toggle_auto_notch();
void cycle_squelch();
void adjust_squelch(int delta_db);
void apply_clean_preset();
void process(int16_t* samples, size_t count, float rf_dbfs);

}  // namespace orcsdr::shortwave::audio_dsp
