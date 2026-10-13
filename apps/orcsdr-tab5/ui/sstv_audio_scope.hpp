#pragma once
// A small audio spectrogram of what the SSTV decoder listens to (12 kHz samples), for the TUNE tab's "sync comb":
// the 1200 Hz line sync, the 1500 Hz black and 2300 Hz white edges, and the header's 1900 Hz leader and 1100/1300 Hz
// VIS bits all show as recognisable traces. Pure C++ (the decoder calls push(); the display reads view()), so it
// is host-tested.
#include <cstddef>
#include <cstdint>

namespace orcsdr::sstv_audio_scope {

constexpr uint32_t kSampleRate = 12000;
constexpr size_t kFft = 512;                 // 23.4 Hz per bin
constexpr size_t kHop = 512;                 // one row every 42.7 ms
constexpr uint16_t kLowHz = 700;
constexpr uint16_t kHighHz = 2700;
constexpr size_t kBins = 86;                 // bins covering kLowHz..kHighHz
constexpr size_t kRows = 96;                 // history kept

// Allocates the history (PSRAM on the device). Safe to call repeatedly; returns false when out of memory.
bool start();
void stop();
bool active();

// Feed decoder-rate samples (any chunk size). Cheap: it copies into a window and runs one FFT per kHop samples.
void push(const int16_t* samples, size_t count);

struct View {
  const uint8_t* rows = nullptr;   // kRows x kBins, row-major, one byte per bin (0 = quiet, 255 = full scale)
  size_t bins = 0;
  size_t row_count = 0;
  uint32_t sequence = 0;           // rows produced since start(); row k of the history is sequence - 1 - k
  uint16_t low_hz = kLowHz;
  uint16_t high_hz = kHighHz;
};
View view();

// Frequency (Hz) at the centre of a bin and the bin nearest a frequency (clamped), for drawing guide rails.
uint32_t bin_hz(size_t bin);
size_t bin_for_hz(uint32_t hz);

bool self_check();

}  // namespace orcsdr::sstv_audio_scope
