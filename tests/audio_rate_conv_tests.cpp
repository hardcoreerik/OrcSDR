// 51.2 kS/s -> 48 kS/s audio converter and the rate plan that selects it.
#include "../apps/orcsdr-tab5/ui/audio_rate_conv.hpp"
#include "../apps/orcsdr-tab5/ui/rtl_rate_plan.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace orcsdr;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
constexpr double kPi = 3.14159265358979323846;

static std::vector<int16_t> tone(double hz, double rate, size_t n, double amp = 10000) {
  std::vector<int16_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = static_cast<int16_t>(std::lround(amp * std::sin(2 * kPi * hz * i / rate)));
  return v;
}
// amplitude and frequency of a tone by fitting sin/cos at the expected frequency over the tail
static void fit(const std::vector<int16_t>& v, double hz, double rate, double* amp) {
  double s = 0, c = 0; const size_t a = v.size() / 4;
  for (size_t i = a; i < v.size(); ++i) { s += v[i] * std::sin(2 * kPi * hz * i / rate); c += v[i] * std::cos(2 * kPi * hz * i / rate); }
  const double n = static_cast<double>(v.size() - a);
  *amp = 2.0 * std::sqrt(s * s + c * c) / n;
}

int main() {
  // Ratio and tone preservation: 1 kHz, 5 kHz, 12 kHz arrive at their frequency and (nearly) full amplitude at 48 kHz.
  for (double hz : {1000.0, 5000.0, 12000.0}) {
    audio_rate_conv::Down16to15 c;
    const auto in = tone(hz, 51200.0, 51200);                  // one second
    std::vector<int16_t> out(in.size());
    const size_t n = c.process(in.data(), in.size(), out.data());
    out.resize(n);
    double amp = 0; fit(out, hz, 48000.0, &amp);
    std::printf("%6.0f Hz: %zu out for %zu in, amplitude %.0f (want 10000)\n", hz, n, in.size(), amp);
    CHECK(n > 47990 && n <= 48000);
    // measured 2026-11-24: 1 kHz 0.0 dB, 5 kHz -0.01 dB, 12 kHz -0.43 dB (cubic interpolation rolls off toward Nyquist)
    CHECK(amp > 8900 && amp < 10100);   // within 1 dB up to 12 kHz
  }
  // In place gives the same answer as separate buffers; any chunking gives the same answer as one call.
  {
    const auto in = tone(3000.0, 51200.0, 4000);
    audio_rate_conv::Down16to15 a, b;
    std::vector<int16_t> o1(in.size()), buf = in;
    const size_t n1 = a.process(in.data(), in.size(), o1.data());
    std::vector<int16_t> o2; size_t pos = 0; const size_t chunks[] = {1, 7, 331, 16, 500};
    size_t ci = 0;
    while (pos < buf.size()) {
      const size_t take = std::min(chunks[ci++ % 5], buf.size() - pos);
      std::vector<int16_t> tmp(buf.begin() + pos, buf.begin() + pos + take);
      const size_t w = b.process(tmp.data(), take, tmp.data());   // in place
      o2.insert(o2.end(), tmp.begin(), tmp.begin() + w); pos += take;
    }
    CHECK(o2.size() == n1);
    bool same = true; for (size_t i = 0; i < n1 && i < o2.size(); ++i) if (o1[i] != o2[i]) same = false;
    CHECK(same);
  }
  // reset starts clean
  { audio_rate_conv::Down16to15 c; const auto in = tone(1000, 51200, 200); std::vector<int16_t> o(200);
    const size_t a = c.process(in.data(), 200, o.data()); c.reset(); std::vector<int16_t> o2(200);
    const size_t b = c.process(in.data(), 200, o2.data()); CHECK(a == b); CHECK(o == o2); }
  // Rate plan
  CHECK(rtl_rate_plan::rf_decimation(2560000) == 10 && rtl_rate_plan::audio_rate(2560000) == 51200);
  CHECK(rtl_rate_plan::rf_decimation(2400000) == 10 && !rtl_rate_plan::audio_needs_conversion(2400000));
  // One-pole rescale keeps the corner: a pole k at 48k and its rescaled twin at 51.2k have the same time constant.
  { const float k = 0.2424f; const float k2 = rtl_rate_plan::rescale_one_pole(k, 51200);
    const double tau48 = -1.0 / (48000.0 * std::log(1.0 - k)), tau51 = -1.0 / (51200.0 * std::log(1.0 - k2));
    std::printf("tau 48k %.3f us, 51.2k %.3f us (k %.4f -> %.4f)\n", tau48 * 1e6, tau51 * 1e6, k, k2);
    CHECK(std::fabs(tau48 - tau51) < 1e-9); CHECK(k2 < k); }
  std::printf("audio_rate_conv_tests: %s\n", g_fail ? "FAILED" : "ok");
  return g_fail ? 1 : 0;
}
