// Re-uses the NXDN host suite's synthetic frame builder (same TU trick). Synthetic IQ only.
#define main nxdn_decoder_test_main
#include "../../components/orcsdr_nxdn/tests/test_decoder.cpp"
#undef main
#include <cmath>
#include <vector>
std::vector<float> dv_test_nxdn_iq48(bool nxdn96, unsigned ran) {
  constexpr double pi = 3.141592653589793;
  std::vector<std::uint8_t> dibits(30, 0);
  for (unsigned i = 0; i < 8; i++) {
    auto f = make_frame(static_cast<std::uint8_t>(ran));
    std::uint16_t pn = 0xe4;
    for (unsigned k = 0; k < 182; ++k) {
      auto bit = pn & 1U;
      auto fb = ((pn >> 4U) ^ pn) & 1U;
      pn = static_cast<std::uint16_t>((pn >> 1U) | (fb << 8U));
      if (k >= 38 && bit) f[10 + k] ^= 2U;
    }
    dibits.insert(dibits.end(), f.begin(), f.end());
  }
  const unsigned sps = nxdn96 ? 10 : 20;
  std::vector<float> out(dibits.size() * sps * 2);
  double phase = 0;
  for (std::size_t i = 0; i < dibits.size() * sps; ++i) {
    std::uint8_t d = dibits[i / sps];
    const double deviation = d == 1 ? 1800 : (d == 0 ? 600 : (d == 2 ? -600 : -1800));
    phase += 2 * pi * (deviation + 350) / 48000;
    out[2 * i] = static_cast<float>(std::cos(phase));
    out[2 * i + 1] = static_cast<float>(std::sin(phase));
  }
  return out;
}
