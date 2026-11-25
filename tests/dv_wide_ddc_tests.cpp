// dv::WideDdc: tuning inside a 2.4 MS/s stream. A tone at the wanted offset must reach the channelizer output at full
// level, the alias that would land on the same channel must be rejected, and moving the offset must follow the signal.
#include "../apps/orcsdr-tab5/ui/dv_channelizer.hpp"
#include "../apps/orcsdr-tab5/ui/dv_wide_ddc.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace orcsdr::dv;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

constexpr double kFs = 2400000.0;
constexpr double kPi = 3.14159265358979323846;

// Sum of complex tones {freq_hz relative to centre, amplitude} as CU8 at 2.4 MS/s, plus small noise.
struct Tone { double hz; double amp; };
static std::vector<uint8_t> make_cu8(const std::vector<Tone>& tones, size_t pairs, size_t start = 0) {
  std::vector<uint8_t> v(pairs * 2);
  unsigned seed = 12345;
  for (size_t n = 0; n < pairs; ++n) {
    double re = 0, im = 0;
    for (const auto& t : tones) {
      const double ph = 2.0 * kPi * t.hz * static_cast<double>(start + n) / kFs;
      re += t.amp * std::cos(ph); im += t.amp * std::sin(ph);
    }
    seed = seed * 1664525u + 1013904223u;
    re += ((seed >> 16) & 0xff) / 255.0 * 0.004 - 0.002;
    seed = seed * 1664525u + 1013904223u;
    im += ((seed >> 16) & 0xff) / 255.0 * 0.004 - 0.002;
    auto q = [](double x) { double r = x * 127.5 + 127.5; return static_cast<uint8_t>(r < 0 ? 0 : r > 255 ? 255 : std::lround(r)); };
    v[2 * n] = q(re); v[2 * n + 1] = q(im);
  }
  return v;
}

// Run wide -> DDC -> Channelizer. Returns the mean magnitude of the 48 kS/s channel output over the last `tail` samples.
static double run_level(WideDdc& ddc, Channelizer& ch, const std::vector<uint8_t>& cu8, size_t tail) {
  std::vector<Iq240> mid(cu8.size() / 2 / 10 + 2);
  const size_t n240 = ddc.process_cu8(cu8.data(), cu8.size() / 2, mid.data(), mid.size());
  std::vector<Iq48> out(n240 / 5 + 2);
  const size_t n48 = ch.process_f32(reinterpret_cast<const float*>(mid.data()), n240, out.data(), out.size());
  if (n48 < tail) return -1;
  double acc = 0;
  for (size_t i = n48 - tail; i < n48; ++i) acc += std::sqrt(double(out[i].re) * out[i].re + double(out[i].im) * out[i].im);
  return acc / static_cast<double>(tail);
}

int main() {
  const size_t kPairs = 240000;   // 100 ms
  // 1. A tone at the wanted offset arrives at full level, wherever in the span it is.
  for (double off : {-1000000.0, -500000.0, -100000.0, 0.0, 137500.0, 500000.0, 1000000.0}) {
    WideDdc ddc; Channelizer ch; ddc.set_offset_hz(static_cast<int32_t>(off));
    const double lvl = run_level(ddc, ch, make_cu8({{off, 0.5}}, kPairs), 400);
    std::printf("tone at %+8.0f Hz -> level %.3f\n", off, lvl);
    CHECK(lvl > 0.45 && lvl < 0.55);
  }
  // 2. Rejection: a tone 25 kHz away (next channel) and the alias tone (wanted + 240 kHz) must be far below the wanted one.
  {
    const double off = 300000.0;
    WideDdc ddc; Channelizer ch; ddc.set_offset_hz(static_cast<int32_t>(off));
    const double neighbour = run_level(ddc, ch, make_cu8({{off + 25000.0, 0.5}}, kPairs), 400);
    ddc.reset(); ch.reset();
    const double alias = run_level(ddc, ch, make_cu8({{off + 240000.0, 0.5}}, kPairs), 400);
    ddc.reset(); ch.reset();
    const double alias2 = run_level(ddc, ch, make_cu8({{off - 240000.0, 0.5}}, kPairs), 400);
    std::printf("neighbour +25k %.5f  alias +240k %.5f  alias -240k %.5f\n", neighbour, alias, alias2);
    CHECK(neighbour < 0.5 * 0.01);   // better than -40 dB (the existing channelizer's job)
    // measured 2026-11-24: alias +240k -60 dB, alias -240k -49 dB (81-tap Hamming); more taps buy more, at more CPU
    CHECK(alias < 0.5 * 0.0056);     // better than -39 dB
    CHECK(alias2 < 0.5 * 0.0056);    // better than -39 dB (measured -49 dB)
  }
  // 3. Two signals in one stream; moving the offset follows each, with nothing stopped.
  {
    WideDdc ddc; Channelizer ch;
    const std::vector<Tone> two = {{-400000.0, 0.4}, {650000.0, 0.25}};
    const auto a = make_cu8(two, kPairs, 0);
    ddc.set_offset_hz(-400000);
    const double l1 = run_level(ddc, ch, a, 400);
    ddc.set_offset_hz(650000);   // retune inside the span; filter state kept
    const auto b = make_cu8(two, kPairs, kPairs);
    const double l2 = run_level(ddc, ch, b, 400);
    std::printf("follow: first %.3f (want .40)  second %.3f (want .25)\n", l1, l2);
    CHECK(l1 > 0.36 && l1 < 0.44);
    CHECK(l2 > 0.22 && l2 < 0.28);
  }
  // 4. Capacity guard: too-small output counts the input as dropped and writes nothing.
  {
    WideDdc ddc; std::vector<uint8_t> v(2000, 128); Iq240 o[2];
    CHECK(ddc.process_cu8(v.data(), 1000, o, 2) == 0 && ddc.dropped_inputs() == 1000);
  }
  // 5. Cost on this host (not the device): ns per input sample.
  {
    WideDdc ddc; Channelizer ch; ddc.set_offset_hz(200000);
    const auto cu8 = make_cu8({{200000.0, 0.5}}, 2400000);   // 1 s
    std::vector<Iq240> mid(240100); std::vector<Iq48> out(48100);
    const auto t0 = std::chrono::steady_clock::now();
    const size_t n = ddc.process_cu8(cu8.data(), 2400000, mid.data(), mid.size());
    (void)ch.process_f32(reinterpret_cast<const float*>(mid.data()), n, out.data(), out.size());
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("host cost: %.1f ms for 1.0 s of 2.4 MS/s (%.2f ns per input sample)\n", ms, ms * 1e6 / 2400000.0);
  }
  std::printf("dv_wide_ddc_tests: %s\n", g_fail ? "FAILED" : "ok");
  return g_fail ? 1 : 0;
}
