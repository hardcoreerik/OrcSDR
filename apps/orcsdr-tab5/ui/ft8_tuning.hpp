#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8::tuning {

// Dial-frequency arithmetic for the expert FT8/FT4/JS8 tuning controls. Pure functions, no hardware: the receiver's own tuning limits are
// enforced by the caller (validate_rtl_tune_frequency); this only keeps entries inside a sane absolute range and parses what a person types.
constexpr uint32_t kMinDialHz = 500000u;       // 0.5 MHz
constexpr uint32_t kMaxDialHz = 1766000000u;   // top of the RTL tuner range

constexpr size_t kStepCount = 6;
constexpr uint32_t kStepsHz[kStepCount] = {10u, 100u, 1000u, 5000u, 10000u, 100000u};
constexpr size_t kDefaultStepIndex = 2;   // 1 kHz

const char* step_label(size_t index);   // "10 Hz", "1 kHz", ...

// Moves `dial_hz` by `detents` steps of `step_hz` (negative = down), clamped to the absolute range. Never wraps.
uint32_t apply_steps(uint32_t dial_hz, int32_t detents, uint32_t step_hz);

// Parses a typed frequency in MHz ("7.078", "14.0745", "7", ".5"): digits and at most one '.', up to 6 decimal places.
// Returns false (and leaves *hz alone) for an empty string, anything else, or a value outside the absolute range.
bool parse_mhz(const char* text, uint32_t* hz);

// "7.078000 MHz" style text for a dial frequency; at least `size` of 20 bytes recommended.
void format_mhz(uint32_t hz, char* out, size_t size);

// Appends a key ('0'-'9', '.', or '<' for backspace) to an entry buffer of at most `capacity - 1` characters. Rejects a second '.', more than
// 6 decimals and a leading run of zeros ("00"). Returns true when the buffer changed.
bool entry_key(char* buffer, size_t capacity, char key);

bool self_check();

}  // namespace orcsdr::ft8::tuning
