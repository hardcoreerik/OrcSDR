// Host test for spectrum_compute: the module must give bit-identical results to the code it was cut out of
// (draw_spectrum() in main.cpp, commit dfb378b0), which is reproduced below as the reference.
#include "spectrum_compute.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace sc = orcsdr::spectrum_compute;
namespace {
constexpr size_t N = sc::kBins;
int g_failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++g_failures; } } while (0)

struct Arrays {
  std::vector<float> window = std::vector<float>(N), real = std::vector<float>(N), imag = std::vector<float>(N),
                     power = std::vector<float>(N), smooth = std::vector<float>(N), peak = std::vector<float>(N),
                     levels = std::vector<float>(N);
  sc::Buffers buffers() { return {window.data(), real.data(), imag.data(), power.data(), smooth.data(), peak.data(), levels.data()}; }
};

// ---- reference: the former code, line for line ----
struct Reference {
  Arrays a;
  size_t first_bin = 0, last_bin = 0;
  float maximum = -120.0f, strongest = -120.0f;
  size_t strongest_bin = N / 2;
  void window() {
    constexpr float kPi = 3.14159265358979323846f;
    for (size_t index = 0; index < N; ++index) a.window[index] = 0.5f - 0.5f * cosf(2.0f * kPi * index / (N - 1));
  }
  void run(const uint8_t* iq, size_t windows, bool trace_valid, bool home_active) {
    const size_t window_bytes = N * 2;
    for (size_t b = 0; b < N; ++b) a.power[b] = 0.0f;
    constexpr float kPi = 3.14159265358979323846f;
    for (size_t w = 0; w < windows; ++w) {
      const uint8_t* base = iq + w * window_bytes;
      float mean_i = 0.0f, mean_q = 0.0f;
      for (size_t index = 0; index < N; ++index) { mean_i += static_cast<float>(base[index * 2]); mean_q += static_cast<float>(base[index * 2 + 1]); }
      mean_i /= N; mean_q /= N;
      for (size_t index = 0; index < N; ++index) {
        a.real[index] = (static_cast<float>(base[index * 2]) - mean_i) * a.window[index];
        a.imag[index] = (static_cast<float>(base[index * 2 + 1]) - mean_q) * a.window[index];
      }
      for (size_t index = 1, reversed = 0; index < N; ++index) {
        size_t bit = N >> 1;
        for (; reversed & bit; bit >>= 1) reversed ^= bit;
        reversed ^= bit;
        if (index < reversed) { std::swap(a.real[index], a.real[reversed]); std::swap(a.imag[index], a.imag[reversed]); }
      }
      for (size_t length = 2; length <= N; length <<= 1) {
        const float angle = -2.0f * kPi / length;
        const float step_real = cosf(angle), step_imaginary = sinf(angle);
        for (size_t base_i = 0; base_i < N; base_i += length) {
          float tr = 1, ti = 0;
          for (size_t offset = 0; offset < length / 2; ++offset) {
            const size_t upper = base_i + offset, lower = upper + length / 2;
            const float pr = a.real[lower] * tr - a.imag[lower] * ti;
            const float pi = a.real[lower] * ti + a.imag[lower] * tr;
            a.real[lower] = a.real[upper] - pr; a.imag[lower] = a.imag[upper] - pi;
            a.real[upper] += pr; a.imag[upper] += pi;
            const float nr = tr * step_real - ti * step_imaginary;
            ti = tr * step_imaginary + ti * step_real; tr = nr;
          }
        }
      }
      for (size_t bin = 0; bin < N; ++bin) {
        const size_t shifted = (bin + N / 2) % N;
        a.power[bin] += a.real[shifted] * a.real[shifted] + a.imag[shifted] * a.imag[shifted];
      }
    }
    const float inv_w = 1.0f / static_cast<float>(windows);
    maximum = -120.0f; strongest = -120.0f; strongest_bin = N / 2;
    for (size_t bin = 0; bin < N; ++bin) {
      const float level = 10.0f * log10f(a.power[bin] * inv_w + 1.0f);
      a.smooth[bin] = trace_valid ? (0.78f * a.smooth[bin] + 0.22f * level) : level;
      if (!trace_valid || level > a.peak[bin]) a.peak[bin] = level;
      else a.peak[bin] = 0.995f * a.peak[bin] + 0.005f * level;
      a.levels[bin] = home_active ? level : a.smooth[bin];
      if (bin >= first_bin && bin < last_bin) {
        maximum = std::max(maximum, std::max(a.levels[bin], a.peak[bin]));
        if (a.levels[bin] > strongest) { strongest = a.levels[bin]; strongest_bin = bin; }
      }
    }
  }
};

bool same(const std::vector<float>& x, const std::vector<float>& y) { return std::memcmp(x.data(), y.data(), N * sizeof(float)) == 0; }

std::vector<uint8_t> make_iq(size_t windows, unsigned seed, bool tone) {
  std::vector<uint8_t> iq(windows * N * 2);
  unsigned s = seed;
  for (size_t i = 0; i < windows * N; ++i) {
    s = s * 1664525u + 1013904223u;
    float noise = static_cast<float>((s >> 16) & 0x3f) - 31.5f;
    float t = tone ? 60.0f * cosf(2.0f * 3.14159265f * 0.123f * static_cast<float>(i)) : 0.0f;
    float q = tone ? 60.0f * sinf(2.0f * 3.14159265f * 0.123f * static_cast<float>(i)) : 0.0f;
    iq[i * 2] = static_cast<uint8_t>(std::clamp(127.5f + noise + t, 0.0f, 255.0f));
    iq[i * 2 + 1] = static_cast<uint8_t>(std::clamp(127.5f + noise * 0.7f + q, 0.0f, 255.0f));
  }
  return iq;
}

void compare_run(size_t windows, bool tone, unsigned seed, size_t first, size_t last) {
  const auto iq = make_iq(windows, seed, tone);
  Reference ref; Arrays mod;
  ref.first_bin = first; ref.last_bin = last;
  ref.window(); sc::init_window(mod.window.data());
  CHECK(same(ref.a.window, mod.window));
  // frame 1: trace not valid; frames 2 and 3: smoothing and peak hold continue, once with home_active
  const bool traces[3] = {false, true, true}, homes[3] = {false, false, true};
  for (int frame = 0; frame < 3; ++frame) {
    ref.run(iq.data(), windows, traces[frame], homes[frame]);
    sc::welch_power(iq.data(), windows, mod.buffers());
    const auto r = sc::update_levels(mod.buffers(), windows, traces[frame], homes[frame], first, last);
    CHECK(same(ref.a.power, mod.power));
    CHECK(same(ref.a.smooth, mod.smooth));
    CHECK(same(ref.a.peak, mod.peak));
    CHECK(same(ref.a.levels, mod.levels));
    CHECK(r.maximum == ref.maximum && r.strongest == ref.strongest && r.strongest_bin == ref.strongest_bin);
  }
}
}  // namespace

int main() {
  compare_run(2, false, 1, 0, N);
  compare_run(2, true, 2, 512, 1536);
  compare_run(1, true, 3, 700, 800);
  compare_run(2, true, 4, 1000, 1000);   // empty range: maximum stays -120
  // the tone shows up where it should: 0.123 cycles/sample -> bin N/2 + 0.123*N
  {
    const auto iq = make_iq(2, 9, true);
    Arrays a; sc::init_window(a.window.data());
    sc::welch_power(iq.data(), 2, a.buffers());
    const auto r = sc::update_levels(a.buffers(), 2, false, true, 0, N);
    const size_t expect = N / 2 + static_cast<size_t>(0.123f * N + 0.5f);
    CHECK(r.strongest_bin + 3 >= expect && r.strongest_bin <= expect + 3);
    CHECK(r.strongest > 20.0f);
  }
  if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; }
  std::puts("PASS spectrum_compute");
  return 0;
}
