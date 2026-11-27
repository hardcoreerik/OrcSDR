// Verifies the digital-voice channeliser ported from the Tab5 branch: 240 kS/s
// CU8 in, 48 kS/s complex float out, with the wanted channel sitting +60 kHz
// above the tuned centre.
//
// The +60 kHz offset is deliberate: it keeps the tuner's DC spike out of the
// channel. A tone at exactly +60 kHz must survive the mixer and land at baseband;
// a DC tone must be rejected. If the offset or the 5:1 ratio were wrong, neither
// assertion would hold.
#include <cmath>
#include <cstdio>
#include <vector>

#include "dv_channelizer.hpp"

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

// CI8: real and imaginary at 8-bit offset binary, interleaved.
void push_tone(std::vector<uint8_t>& cu8, double hz, uint32_t rate, size_t pairs, double phase0) {
  for (size_t n = 0; n < pairs; ++n) {
    const double phase = phase0 + 2.0 * 3.14159265358979323846 * hz * double(n) / double(rate);
    const int re = int(64 * std::cos(phase));
    const int im = int(64 * std::sin(phase));
    cu8.push_back(uint8_t(std::max(0, std::min(255, 128 + re))));
    cu8.push_back(uint8_t(std::max(0, std::min(255, 128 + im))));
  }
}

double measure(double hz) {
  orcsdr::dv::Channelizer channelizer;
  std::vector<uint8_t> cu8;
  const size_t pairs = 240000 / 2;  // half a second
  push_tone(cu8, hz, 240000, pairs, 0.0);
  std::vector<orcsdr::dv::Iq48> out(pairs / 5 + 16);
  size_t produced = 0;
  for (size_t offset = 0; offset < pairs; offset += 2400) {
    const size_t chunk = std::min<size_t>(2400, pairs - offset);
    produced += channelizer.process_cu8(cu8.data() + offset * 2, chunk, out.data(), out.size());
  }
  // take_level() consumes the accumulated power, so read it exactly once.
  const double level = double(channelizer.take_level());
  std::printf("       %.1f Hz tone -> %zu output samples, level %.4f\n", hz, produced,
              level);
  return level;
}

}  // namespace

int main() {
  std::printf("Digital voice channeliser\n");

  const double on_channel = measure(60000.0);   // the wanted channel
  const double dc = measure(0.0);               // the tuner spike
  const double far = measure(120000.0);         // outside the 12.5 kHz channel

  check(on_channel > 0.3, "a +60 kHz tone survives the mixer at full level");
  check(on_channel > dc * 4.0, "the DC spike is rejected relative to the channel");
  check(on_channel > far * 4.0, "+120 kHz is rejected relative to the channel");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
