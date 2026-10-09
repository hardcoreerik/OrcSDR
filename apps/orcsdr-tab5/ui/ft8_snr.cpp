#include "ft8_snr.hpp"

#include "ft8_ldpc.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace orcsdr::ftx::snr {
namespace {
std::atomic<float> g_user_offset_db{0.0f};
// Guard bins for the noise reference, in tone widths outside the occupied band. The spectral grid uses one rectangular window per
// symbol, whose sidelobes decay slowly, so a strong tone still leaks a little into the nearest guard bins; staying far from it keeps
// that leakage small.
constexpr std::size_t kGuardNear = 8;
constexpr std::size_t kGuardFar = 14;
// Fraction of a tone's power that still lands in each guard bin through those sidelobes, measured with signals of known strength
// (tools/ft8-snr-sweep.cpp). It is a property of the spectral grid, not of the signal, and is removed from the noise reference.
constexpr double kGuardLeakage = 3.5e-4;
}  // namespace


void set_user_offset_db(float offset_db) {
  g_user_offset_db.store(std::max(-20.0f, std::min(20.0f, offset_db)), std::memory_order_relaxed);
}
float user_offset_db() { return g_user_offset_db.load(std::memory_order_relaxed); }

bool estimate(const ModeProfile& profile, const sync::EnergyGrid& grid, const sync::Geometry& geometry,
              const sync::Candidate& candidate, const orcsdr::ft8::codec::MessageBits& message_bits,
              const Calibration& calibration, float* snr_db) {
  if (snr_db == nullptr || grid.cells == nullptr || geometry.rows_per_symbol == 0 || geometry.bins_per_tone == 0 ||
      profile.tone_count < 2 || profile.bits_per_tone == 0 || profile.bits_per_tone > 3)
    return false;
  const std::size_t last_bin =
      static_cast<std::size_t>(candidate.base_bin) + static_cast<std::size_t>(profile.tone_count - 1) * geometry.bins_per_tone;
  if (last_bin >= grid.bins) return false;

  const auto codeword = orcsdr::ft8::ldpc::encode(message_bits);
  constexpr std::size_t kMaxNoiseSamples = 768;
  float noise_samples[kMaxNoiseSamples];
  std::size_t noise_count = 0;
  // Noise reference: bins 3 to 6 tone widths outside the occupied band, on every symbol row of the frame. The "empty" tones
  // inside the band are not clean: timing error makes the analysis window straddle two symbols, and a strong tone splatters
  // into its neighbours, so on strong signals they read high and the estimate saturates. The median of the guard bins ignores
  // other stations in them.
  {
    const std::size_t t = geometry.bins_per_tone;
    const std::size_t low_end = candidate.base_bin >= kGuardNear * t ? candidate.base_bin - kGuardNear * t : 0;
    const std::size_t low_start = candidate.base_bin >= kGuardFar * t ? candidate.base_bin - kGuardFar * t : 0;
    const std::size_t high_start = last_bin + kGuardNear * t + 1;
    const std::size_t high_end = std::min(last_bin + kGuardFar * t + 1, grid.bins);
    for (std::size_t symbol = 0; symbol < profile.channel_symbols; ++symbol) {
      const std::size_t row = static_cast<std::size_t>(candidate.start_row) + symbol * geometry.rows_per_symbol;
      if (row >= grid.rows) break;
      const float* cells = grid.cells + row * grid.stride;
      for (std::size_t bin = low_start; bin < low_end && noise_count < kMaxNoiseSamples; ++bin)
        if (std::isfinite(cells[bin]) && cells[bin] >= 0.0f) noise_samples[noise_count++] = cells[bin];
      for (std::size_t bin = high_start; bin < high_end && noise_count < kMaxNoiseSamples; ++bin)
        if (std::isfinite(cells[bin]) && cells[bin] >= 0.0f) noise_samples[noise_count++] = cells[bin];
    }
  }
  const bool guard_ok = noise_count >= 40;
  if (!guard_ok) noise_count = 0;   // at the edge of the grid: fall back to the empty tones below
  double tx_sum = 0.0;
  std::size_t symbols = 0;
  std::size_t bit_index = 0;
  for (std::size_t block_index = 0; block_index < profile.data_block_count; ++block_index) {
    const auto& block = profile.data[block_index];
    for (std::size_t offset = 0; offset < block.length; ++offset) {
      // The transmitted tone for this symbol: the tone whose bit pattern equals the codeword bits (MSB first), as in the demodulator.
      uint8_t pattern = 0;
      for (uint8_t b = 0; b < profile.bits_per_tone; ++b) {
        if (bit_index >= codeword.size()) return false;
        pattern = static_cast<uint8_t>((pattern << 1) | (codeword[bit_index++] & 1u));
      }
      int tx = -1;
      for (uint8_t tone = 0; tone < profile.tone_count; ++tone)
        if (profile.tone_bits[tone] == pattern) {
          tx = tone;
          break;
        }
      if (tx < 0) return false;
      const std::size_t row =
          static_cast<std::size_t>(candidate.start_row) + (block.first_symbol + offset) * geometry.rows_per_symbol;
      if (row >= grid.rows) return false;
      for (uint8_t tone = 0; tone < profile.tone_count; ++tone) {
        const float value = grid.cells[row * grid.stride + candidate.base_bin + static_cast<std::size_t>(tone) * geometry.bins_per_tone];
        if (!std::isfinite(value) || value < 0.0f) return false;
        if (tone == tx) tx_sum += value;
        else if (!guard_ok && noise_count < kMaxNoiseSamples) noise_samples[noise_count++] = value;
      }
      ++symbols;
    }
  }
  if (symbols == 0) return false;
  if (noise_count == 0) return false;
  const double tx = tx_sum / symbols;       // signal + noise in one tone bin
  // Noise alone in one tone bin. The median of the empty tones, not their mean: a strong tone leaks into its neighbours, which
  // would inflate the mean and make strong signals read low. The energy of Gaussian noise in one bin is exponentially
  // distributed, whose median is ln(2) times its mean, so the median is scaled back by 1/ln(2).
  // The reference bins are often shared with neighbouring stations (a busy band), so use a low percentile rather than the
  // median: the 25th percentile of an exponential distribution is -ln(0.75) = 0.2877 of its mean, and it stays clean while up
  // to three quarters of the bins hold someone else's signal.
  const std::size_t quantile_index = noise_count / 4;
  std::nth_element(noise_samples, noise_samples + quantile_index, noise_samples + noise_count);
  const double noise = static_cast<double>(noise_samples[quantile_index]) / 0.2876820724517809;
  if (!(noise > 0.0)) return false;
  // Remove the tone's own leakage from the noise reference: noise_measured = noise + leak * signal.
  double signal = (tx - noise) / (1.0 - kGuardLeakage);
  double noise_clean = noise - kGuardLeakage * signal;
  if (noise_clean < 0.05 * noise) noise_clean = 0.05 * noise;
  // Noise power in one tone bin spans the tone spacing; scale it to 2500 Hz.
  const double spacing_hz = static_cast<double>(profile.tone_spacing_millihz) / 1000.0;
  float result = kFloorDb;
  if (signal > 0.0) {
    const double ratio_db = 10.0 * std::log10(signal / noise_clean) - 10.0 * std::log10(2500.0 / spacing_hz);
    result = static_cast<float>(ratio_db) + calibration.offset_db + g_user_offset_db.load(std::memory_order_relaxed);
  }
  if (result < kFloorDb) result = kFloorDb;
  *snr_db = result;
  return true;
}

bool self_check() { return kFloorDb < 0.0f; }

}  // namespace orcsdr::ftx::snr
