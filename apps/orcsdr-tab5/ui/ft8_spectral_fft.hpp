#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::ftx::spectral_fft {

// Fast, float-only replacement for the exact-correlation spectral oracle (ft8_spectral) on the symbol grid.
//
// The oracle's bins sit at multiples of fs / symbol_samples (6.25 Hz for FT8, 20.83 Hz for FT4), so each row is an
// ordinary symbol_samples-point DFT, rectangular window, power |X|^2 / n^2 - the same definition. This computes that DFT
// with a mixed-radix transform (symbol_samples = odd N1 x power of two N2) and keeps only the requested bin range.
// Rows are `hop` samples apart; `rows_per_symbol` 2 means hop = symbol_samples / 2. Float throughout: the ESP32-P4 FPU is
// single precision.
constexpr size_t kMaxN1 = 15;
constexpr size_t kMaxN2 = 256;   // 3840 = 15 x 256 is the largest transform used (FT8 half-bin grid)

struct Plan {
  size_t n = 0;
  size_t n1 = 0;
  size_t n2 = 0;
  // Twiddles W_N^(n2*k1) for the middle step: n2 * n1 complex values
  std::array<float, kMaxN1 * kMaxN2> tw_re{};
  std::array<float, kMaxN1 * kMaxN2> tw_im{};
  // 128-point (n2) radix-2 twiddles and the n1-point DFT matrix
  std::array<float, kMaxN2> w2_re{};
  std::array<float, kMaxN2> w2_im{};
  std::array<float, kMaxN1 * kMaxN1> w1_re{};
  std::array<float, kMaxN1 * kMaxN1> w1_im{};
  std::array<uint16_t, kMaxN2> bitrev{};
};

struct Scratch {
  // [n1][n2] complex work array
  std::array<float, kMaxN1 * kMaxN2> re{};
  std::array<float, kMaxN1 * kMaxN2> im{};
};

// n must factor as an odd n1 <= 15 times a power-of-two n2 <= 256 (1920 = 15 x 128 and 576 = 9 x 64 qualify).
bool make_plan(Plan* plan, size_t n);

// One DFT of `plan.n` points over `input_len` real samples (the rest are zero, i.e. a zero-padded window: with input_len = n/2
// the bins fall at half the natural spacing); writes power for bins [first_bin, first_bin + bin_count) to `out`.
void power_bins(const Plan& plan, Scratch* scratch, const int16_t* samples, size_t input_len, size_t first_bin, size_t bin_count,
                float* out);

// All rows for `count` samples: row r uses samples [r*hop, r*hop + input_len). Returns the number of rows written (<= row_capacity).
size_t power_rows(const Plan& plan, Scratch* scratch, const int16_t* samples, size_t count, size_t input_len, size_t hop,
                  size_t first_bin, size_t bin_count, float* grid, size_t grid_stride, size_t row_capacity);

bool self_check();

}  // namespace orcsdr::ftx::spectral_fft
