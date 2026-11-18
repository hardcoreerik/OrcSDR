// The folded (symmetric) FIR path of the FT8 audio tap must give the same audio as the plain loop, and the audio must still carry a tone
// placed above the dial. Compares the two implementations on a noisy two-tone IQ block.
#include "../ui/ft8_audio_tap.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace orcsdr::ftx::audio_tap;

int main() {
  std::vector<uint8_t> iq(2 * 240000 / 2);   // 0.5 s at 240 kS/s
  uint32_t seed = 7;
  for (size_t n = 0; n < iq.size() / 2; ++n) {
    seed = seed * 1664525u + 1013904223u;
    const double noise = (static_cast<int>(seed >> 24) - 128) * 0.1;
    const double t = static_cast<double>(n) / 240000.0;
    const double tone = 40.0 * std::cos(2.0 * M_PI * 1000.0 * t);   // 1 kHz above the dial: inside the USB passband
    const double tone_q = 40.0 * std::sin(2.0 * M_PI * 1000.0 * t);
    iq[2 * n] = static_cast<uint8_t>(std::lround(127.5 + tone + noise));
    iq[2 * n + 1] = static_cast<uint8_t>(std::lround(127.5 + tone_q + noise));
  }
  Tap* a = new Tap();
  Tap* b = new Tap();
  assert(begin(a, 240000) && begin(b, 240000));
  std::vector<int16_t> out_a(8000), out_b(8000);
  size_t na = 0, nb = 0;
  for (size_t off = 0; off + 8192 <= iq.size(); off += 8192) {
    na += process_cu8(a, iq.data() + off, 8192, out_a.data() + na, out_a.size() - na);
    nb += process_cu8_ref(b, iq.data() + off, 8192, out_b.data() + nb, out_b.size() - nb);
  }
  assert(na == nb && na > 1000);
  int max_diff = 0;
  double power = 0.0;
  for (size_t i = 0; i < na; ++i) {
    max_diff = std::max(max_diff, std::abs(out_a[i] - out_b[i]));
    power += static_cast<double>(out_a[i]) * out_a[i];
  }
  std::printf("samples=%zu max_diff=%d rms=%.0f\n", na, max_diff, std::sqrt(power / na));
  assert(max_diff <= 2);               // float summation order only
  assert(std::sqrt(power / na) > 500); // the tone is really in the output
  std::puts("FT8 audio tap fold test PASS");
  delete a;
  delete b;
}
