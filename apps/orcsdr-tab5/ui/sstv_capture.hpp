#pragma once
// Records the audio the SSTV decoder actually receives (its 12 kHz decoder-rate stream) into memory and writes it out as a
// mono 16-bit WAV, so a reception that failed at the edge of sensitivity can be replayed offline, sample for sample, while
// the decoder or the receiver is changed one thing at a time. Pure C++ apart from the allocation (PSRAM on the device), so it
// is host-tested.
#include <cstddef>
#include <cstdint>

namespace orcsdr::sstv_capture {

constexpr uint32_t kSampleRate = 12000;
constexpr uint32_t kMaxSeconds = 60;

// Allocates room for `seconds` and starts recording from the next sample. False when out of memory or seconds is 0 or too large.
bool start(uint32_t seconds);
bool recording();
// Called with decoder-rate samples (any chunk size); ignored when not recording, and recording stops by itself when full.
void push(const int16_t* samples, size_t count);
// Stops recording (the data stays until the next start or release).
void stop();
size_t samples();
bool full();
// Writes what was recorded as a WAV. The writer receives the bytes in order; returns false when it refuses any of them.
using Writer = bool (*)(void* context, const void* data, size_t size);
bool write_wav(Writer writer, void* context);
void release();

bool self_check();

}  // namespace orcsdr::sstv_capture
