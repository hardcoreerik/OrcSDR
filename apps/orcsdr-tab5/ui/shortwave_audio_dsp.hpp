#pragma once

#include <array>
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

#if ORCSDR_DSP_AB
struct State {
  bool squelch_open = true;
  bool notch_active = false;
  uint16_t notch_hz = 0;
  float input_rms_dbfs = -120.0f;
  float last_rf_dbfs = -90.0f;
  bool reset_requested = true;
  bool notch_reset_requested = false;
  bool auto_floor_reset_requested = false;
  float low = 0.0f;
  float voice = 0.0f;
  std::array<float, 3> env{};
  std::array<float, 3> noise{};
  std::array<float, 3> gain{};
  std::array<float, 25> goertzel1{};
  std::array<float, 25> goertzel2{};
  size_t tone_samples = 0;
  int candidate_bin = -1;
  int candidate_frames = 0;
  int missed_frames = 0;
  float notch_x1 = 0.0f;
  float notch_x2 = 0.0f;
  float notch_y1 = 0.0f;
  float notch_y2 = 0.0f;
  float notch_cosine = 0.0f;
  float gate = 1.0f;
  float auto_floor = -90.0f;
  uint32_t squelch_hang = 0;
};

State snapshot_state();
void restore_state(const State& s);
#endif

}  // namespace orcsdr::shortwave::audio_dsp
