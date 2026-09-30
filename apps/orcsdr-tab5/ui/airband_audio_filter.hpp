#pragma once

#include <cstddef>
#include <cstdint>

// Voice band-pass for airband AM audio. The AM envelope detector produces hiss across the whole
// 0..24 kHz audio band, but aircraft voice only occupies about 300 Hz to 3 kHz, so keeping just that
// band removes most of the noise energy and any DC/rumble without touching the speech. Pure C++
// (host-tested); the application applies it to the demodulated audio before it is queued.
namespace orcsdr::airband_audio {

constexpr float kSampleRateHz = 48000.0f;
constexpr float kHighPassHz = 300.0f;
constexpr float kLowPassHz = 3200.0f;

// Clears the filter state; call when the tuned channel or stream changes so no old audio rings.
void reset();

// Filters `count` signed 16-bit samples in place.
void process(int16_t* samples, size_t count);

// Gain (linear) of the whole filter at a frequency; used by the host tests.
float response(float frequency_hz);

}  // namespace orcsdr::airband_audio
