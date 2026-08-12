#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr::fm {

constexpr uint32_t kPcmRateHz = 48000;
constexpr size_t kSpectrumBins = 64;

/** Allocate PSRAM ring; call once at boot. */
bool begin();

/** Reset filters and clear ring (on retune / mode enter). */
void reset();

/**
 * Demodulate CU8 IQ @ 960 kS/s into 48 kHz mono PCM ring.
 * Safe from RF callback; keep light.
 * wbfm=true: broadcast FM de-emphasis; false: NFM/WX tighter path.
 */
void process_cu8(const uint8_t* iq, size_t bytes, bool wbfm);

/** Pull up to max_samples of new PCM. Returns count written. */
size_t pull_pcm(int16_t* out, size_t max_samples);

/** Monotonic PCM sample sequence (total produced). */
uint32_t pcm_sequence();

/** Samples currently buffered. */
size_t pcm_available();

float signal_dbfs();
void copy_spectrum(float* out_bins, size_t count);

uint32_t underruns();
uint32_t overruns();

}  // namespace orcsdr::fm
