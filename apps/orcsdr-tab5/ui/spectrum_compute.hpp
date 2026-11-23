#pragma once
// The number crunching of the RF scope, moved out of main.cpp's draw_spectrum(): the Hann window, the Welch-averaged FFT
// power and the smoothed / peak-held levels. No device headers, no state of its own: the arrays live in PSRAM in main.cpp
// and are handed in. draw_spectrum() keeps the screen, the timing and the per-dashboard hand-off.
#include <cstddef>
#include <cstdint>

namespace orcsdr::spectrum_compute {

constexpr size_t kBins = 2048;   // main.cpp static_asserts that this is kRtlSpectrumBins

struct Buffers {
  float* window;      // Hann window, filled by init_window()
  float* real;        // FFT work
  float* imaginary;   // FFT work
  float* power;       // Welch sum per bin
  float* smooth;      // EMA of the level
  float* peak;        // slow peak hold
  float* levels;      // what the dashboards draw
};

void init_window(float* window);

// Welch-averages `windows` blocks of 8-bit IQ (kBins complex samples each, back to back in `iq`) into buffers.power.
void welch_power(const uint8_t* iq, size_t windows, const Buffers& buffers);

struct Levels {
  float maximum;          // strongest of levels and peak inside [first_bin, last_bin)
  float strongest;        // strongest level inside the range
  size_t strongest_bin;
};

// power -> dB, then smooth / peak / levels. `trace_valid` is false on the first frame after a reset; `home_active` makes
// `levels` the raw level instead of the smoothed one.
Levels update_levels(const Buffers& buffers, size_t windows, bool trace_valid, bool home_active,
                     size_t first_bin, size_t last_bin);

}  // namespace orcsdr::spectrum_compute
