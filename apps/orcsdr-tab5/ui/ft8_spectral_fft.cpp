#include "ft8_spectral_fft.hpp"

#include <cmath>
#include <cstring>

namespace orcsdr::ftx::spectral_fft {
namespace {

constexpr double kPi = 3.14159265358979323846;

bool is_pow2(size_t v) { return v != 0 && (v & (v - 1)) == 0; }

}  // namespace

bool make_plan(Plan* plan, size_t n) {
  if (plan == nullptr || n < 8) return false;
  size_t n2 = 1;
  while (n % (n2 * 2) == 0 && n2 * 2 <= kMaxN2) n2 *= 2;
  const size_t n1 = n / n2;
  if (n1 * n2 != n || n1 == 0 || n1 > kMaxN1 || !is_pow2(n2) || n2 < 2) return false;

  *plan = Plan{};
  plan->n = n;
  plan->n1 = n1;
  plan->n2 = n2;
  // middle twiddles W_N^(b*c): index c * n2 + b
  for (size_t c = 0; c < n1; ++c)
    for (size_t b = 0; b < n2; ++b) {
      const double ang = -2.0 * kPi * static_cast<double>((b * c) % n) / static_cast<double>(n);
      plan->tw_re[c * n2 + b] = static_cast<float>(std::cos(ang));
      plan->tw_im[c * n2 + b] = static_cast<float>(std::sin(ang));
    }
  for (size_t m = 0; m < n2 / 2; ++m) {
    const double ang = -2.0 * kPi * static_cast<double>(m) / static_cast<double>(n2);
    plan->w2_re[m] = static_cast<float>(std::cos(ang));
    plan->w2_im[m] = static_cast<float>(std::sin(ang));
  }
  for (size_t a = 0; a < n1; ++a)
    for (size_t c = 0; c < n1; ++c) {
      const double ang = -2.0 * kPi * static_cast<double>((a * c) % n1) / static_cast<double>(n1);
      plan->w1_re[a * n1 + c] = static_cast<float>(std::cos(ang));
      plan->w1_im[a * n1 + c] = static_cast<float>(std::sin(ang));
    }
  size_t bits = 0;
  while ((static_cast<size_t>(1) << bits) < n2) ++bits;
  for (size_t i = 0; i < n2; ++i) {
    size_t r = 0;
    for (size_t b = 0; b < bits; ++b)
      if (i & (static_cast<size_t>(1) << b)) r |= static_cast<size_t>(1) << (bits - 1 - b);
    plan->bitrev[i] = static_cast<uint16_t>(r);
  }
  return true;
}

void power_bins(const Plan& plan, Scratch* scratch, const int16_t* samples, size_t first_bin, size_t bin_count, float* out) {
  const size_t n = plan.n, n1 = plan.n1, n2 = plan.n2;
  float* re = scratch->re.data();
  float* im = scratch->im.data();

  // Step 1 and 2: n1-point DFT over a for every b, then the middle twiddle; stored bit-reversed along b.
  for (size_t b = 0; b < n2; ++b) {
    float xs[kMaxN1];
    for (size_t a = 0; a < n1; ++a) xs[a] = static_cast<float>(samples[n2 * a + b]);
    const size_t pos = plan.bitrev[b];
    for (size_t c = 0; c < n1; ++c) {
      float yr = 0.0f, yi = 0.0f;
      for (size_t a = 0; a < n1; ++a) {
        yr += xs[a] * plan.w1_re[a * n1 + c];
        yi += xs[a] * plan.w1_im[a * n1 + c];
      }
      const float tr = plan.tw_re[c * n2 + b], ti = plan.tw_im[c * n2 + b];
      re[c * n2 + pos] = yr * tr - yi * ti;
      im[c * n2 + pos] = yr * ti + yi * tr;
    }
  }

  // Step 3: in-place radix-2 DIT over b for each c (input already bit-reversed).
  for (size_t c = 0; c < n1; ++c) {
    float* r = re + c * n2;
    float* i = im + c * n2;
    for (size_t len = 2; len <= n2; len <<= 1) {
      const size_t half = len / 2;
      const size_t stride = n2 / len;
      for (size_t start = 0; start < n2; start += len)
        for (size_t k = 0; k < half; ++k) {
          const float wr = plan.w2_re[k * stride], wi = plan.w2_im[k * stride];
          const size_t p = start + k, q = p + half;
          const float tr = r[q] * wr - i[q] * wi;
          const float ti = r[q] * wi + i[q] * wr;
          r[q] = r[p] - tr;
          i[q] = i[p] - ti;
          r[p] += tr;
          i[p] += ti;
        }
    }
  }

  const float inv = 1.0f / (static_cast<float>(n) * static_cast<float>(n));
  for (size_t j = 0; j < bin_count; ++j) {
    const size_t k = first_bin + j;
    const size_t c = k % n1, d = k / n1;
    const float vr = re[c * n2 + d], vi = im[c * n2 + d];
    out[j] = (vr * vr + vi * vi) * inv;
  }
}

size_t power_rows(const Plan& plan, Scratch* scratch, const int16_t* samples, size_t count, size_t hop, size_t first_bin,
                  size_t bin_count, float* grid, size_t grid_stride, size_t row_capacity) {
  if (plan.n == 0 || hop == 0 || count < plan.n || first_bin + bin_count > plan.n / 2) return 0;
  size_t rows = 0;
  for (size_t start = 0; start + plan.n <= count && rows < row_capacity; start += hop, ++rows)
    power_bins(plan, scratch, samples + start, first_bin, bin_count, grid + rows * grid_stride);
  return rows;
}

bool self_check() {
  Plan p;
  return make_plan(&p, 1920) && p.n1 == 15 && p.n2 == 128 && make_plan(&p, 576) && p.n1 == 9 && p.n2 == 64 &&
         !make_plan(&p, 1921) && !make_plan(&p, 0);
}

}  // namespace orcsdr::ftx::spectral_fft
