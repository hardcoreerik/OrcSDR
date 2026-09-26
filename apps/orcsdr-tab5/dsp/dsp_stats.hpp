#pragma once

#include <cstddef>
#include <cstdint>

// Low-overhead DSP task instrumentation (per-stage time, load, queue depth).
// Stages are timed with two micros() reads each; counters are atomics read and
// reset by the RTL_DSP STATS serial command, so nothing prints unless asked.
namespace orcsdr::dsp::stats {

enum class Stage : uint8_t {
  level,     // signal level / clipping over the raw block
  spectrum,  // spectrum snapshot copy
  decoders,  // POCSAG, P25, LoRa, ADS-B hand-off
  record,    // IQ recording append
  demod,     // FM/AM/SSB demodulation and audio
  other,     // am_finder and anything not attributed above
  count
};

void add(Stage stage, uint32_t elapsed_us);
// One IQ block finished: total task time, samples, and filled-queue depth seen
// when the block was taken (depth > 0 means the task is falling behind).
void block_done(uint32_t total_us, uint32_t samples, uint32_t queue_depth);
void overload_yield();

// Formats the window since the last call and resets it. Returns bytes written.
size_t format_and_reset(char* out, size_t size, uint32_t window_ms);
const char* stage_name(Stage stage);
bool self_check();

}  // namespace orcsdr::dsp::stats
