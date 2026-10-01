#pragma once

#include <cstdint>

// Frequency-driven receiver choices for the Home screen's "tune anywhere" mode (the BROWSE band):
// which demodulator a frequency calls for, and the steps Home offers there. Pure C++, host-tested.
namespace orcsdr::band_plan {

// The span the receiver driver can tune; mirrors ESP_RTL_SDR_FREQ_MIN_HZ / _MAX_HZ.
constexpr uint32_t kMinHz = 24000u;
constexpr uint32_t kMaxHz = 1766000000u;

enum class Demod : uint8_t { nfm, wfm, am };

// The mode a frequency normally uses: wide FM for the 87.5-108 MHz broadcast band, AM for the
// 118-137 MHz airband and for HF (24 kHz to 30 MHz), narrow FM for everything else.
Demod demod_for(uint32_t frequency_hz);
const char* demod_name(Demod demod);

// Clamps to what the driver can tune.
uint32_t clamp(uint32_t frequency_hz);

// Tuning steps for Home in BROWSE: 1, 5, 6.25, 8.33, 10, 12.5, 25, 50, 100, 200, 500 kHz and 1 MHz.
constexpr uint32_t kStepsHz[] = {1000,  5000,  6250,   8333,   10000,  12500,
                                 25000, 50000, 100000, 200000, 500000, 1000000};
constexpr uint32_t kDefaultStepHz = 12500;
// The next larger (direction > 0) or smaller step in the list; an unlisted value snaps to the nearest first.
uint32_t cycle_step(uint32_t current_hz, int direction);

// frequency +/- step, clamped to the driver's range.
uint32_t step_frequency(uint32_t frequency_hz, uint32_t step_hz, int direction);

// Parses a typed frequency ("96.1", "118.9", "1090", "7.074", ".5") as MHz into Hz. Values of 2000 or
// more are taken as kHz so "5000" means 5 MHz. Returns 0 when it is not a valid frequency the driver can tune.
uint32_t parse_mhz(const char* text);

}  // namespace orcsdr::band_plan
