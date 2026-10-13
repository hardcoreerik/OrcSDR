#pragma once
// Tuning logic for the SSTV dashboard's TUNE tab: VFO steps, demodulator choice, tap-to-tune on the spectrum,
// the tuning-error readout and the waterfall's colour palette. Pure C++ (no display or ESP-IDF headers) so it is
// host-tested.
#include <cstddef>
#include <cstdint>

namespace orcsdr::sstv_tune {

enum class Demod : uint8_t { nfm, usb, lsb };
const char* demod_name(Demod demod);

// The usual demodulator for a frequency: LSB below 10 MHz, USB from 10 to 30 MHz (amateur SSTV convention),
// narrow FM above that.
Demod default_demod(uint32_t frequency_hz);

// Receiver limits: the RTL-SDR Blog V4's HF upconverter reaches down to 100 kHz; the tuner tops out at 1.7 GHz.
constexpr uint32_t kMinHz = 100000;
constexpr uint32_t kMaxHz = 1700000000;
uint32_t clamp_hz(uint32_t frequency_hz);

// VFO steps. step_index() returns the position of a step on the list (the default for an unknown value).
size_t step_count();
uint32_t step_hz(size_t index);
size_t step_index(uint32_t step);
// A sensible starting step for the demodulator: fine for SSB, a channel for FM.
uint32_t default_step(Demod demod);

// Moves to the next multiple of `step` in the given direction (so repeated presses stay on a clean grid even when
// the starting frequency is not on it) and clamps to the receiver's range.
uint32_t nudge(uint32_t frequency_hz, uint32_t step, int direction);

// Tap on the spectrum: the frequency under pixel x of a plot `width` pixels wide that spans `span_hz` centred on
// `center_hz`, rounded to `step` and clamped. to_x() is the inverse (clamped to the plot).
uint32_t from_touch(uint32_t center_hz, uint32_t span_hz, int x, int width, uint32_t step);
int to_x(uint32_t center_hz, uint32_t span_hz, uint32_t frequency_hz, int width);

// Frequency typed on the keypad: "145.5" is MHz, a whole number of five or more digits ("14230", "145500") is kHz,
// anything shorter ("145") is MHz. False when it is empty, malformed or outside the receiver's range.
bool parse_entry(const char* text, uint32_t* frequency_hz);
// "145.50000" (MHz, five decimals) for the big VFO readout.
void format_mhz(char* out, size_t size, uint32_t frequency_hz);

// Where the SSTV sync pulse should sit in the audio (Hz), and how far off it measures.
constexpr float kSyncToneHz = 1200.0f;
struct TuneError {
  bool valid = false;
  int32_t correction_hz = 0;   // how far to move the dial: positive = raise the dial frequency
};
// In SSB the audio is the offset from the dial, so a sync tone reading high on USB means the dial is too low (raise it) and
// on LSB the opposite. In FM the demodulated tone does not depend on the dial, so no correction applies. `pulses` is
// how many sync pulses the estimate averages (too few and the readout is not trusted).
TuneError tuning_error(Demod demod, float measured_sync_hz, uint32_t pulses);

// Palette for the SSTV waterfall: 0..255 level to RGB565. A dedicated teal-to-amber-to-white ramp that is
// recognisably different from the FT8 and Home palettes.
uint16_t palette565(uint8_t level);
// Tint applied to guide rails and the frame, from the receiver state (see SessionState in sstv_receiver.hpp).
enum class Tint : uint8_t { searching, header, receiving, recovering };
uint16_t tint565(Tint tint);

bool self_check();

}  // namespace orcsdr::sstv_tune
