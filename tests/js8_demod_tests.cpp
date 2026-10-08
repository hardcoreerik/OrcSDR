#include "js8_demod.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr std::array<uint8_t, orcsdr::js8::kChannelSymbols> kApiFrame{{
    4,2,5,6,1,3,0, 1,0,2,6,6,3,1,6,6,4,0,1,7,0,7,2,6,2,6,0,4,3,4,5,2,3,5,2,0,
    4,2,5,6,1,3,0, 3,4,2,5,4,5,7,0,1,6,3,6,7,0,2,3,5,6,4,5,7,4,0,0,1,7,3,6,4,
    4,2,5,6,1,3,0}};

uint32_t noise_state = 0x12345678u;
float noise() {
  noise_state ^= noise_state << 13;
  noise_state ^= noise_state >> 17;
  noise_state ^= noise_state << 5;
  return static_cast<float>(static_cast<int32_t>(noise_state & 0xffffu) - 32768) / 32768.0f;
}
}  // namespace

int main() {
  using namespace orcsdr::js8;
  assert(self_check_demod());
  const Profile& p = profile(Submode::normal);
  constexpr size_t start = 733;
  const size_t frame_samples = static_cast<size_t>(p.channel_symbols) * p.symbol_samples;
  std::vector<int16_t> pcm(start + frame_samples + 100, 0);
  const float base_hz = 900.0f;
  float phase = 0.0f;
  for (size_t symbol = 0; symbol < kApiFrame.size(); ++symbol) {
    const float hz = base_hz + kApiFrame[symbol] * 6.25f;
    const float step = 2.0f * kPi * hz / 12000.0f;
    for (size_t n = 0; n < p.symbol_samples; ++n) {
      const float sample = 12000.0f * std::sin(phase) + 450.0f * noise();
      pcm[start + symbol * p.symbol_samples + n] = static_cast<int16_t>(sample);
      phase += step;
      if (phase > 2.0f * kPi) phase -= 2.0f * kPi;
    }
  }

  DemodConfig cfg{};
  cfg.start_sample = start;
  cfg.base_hz = base_hz;
  RawFrame frame{};
  DemodStats stats{};
  assert(demodulate_tones(pcm.data(), pcm.size(), Submode::normal, cfg, &frame, &stats));
  assert(frame.tones == kApiFrame);
  assert(stats.sync_hits == 21);
  assert(stats.sync_score > 0.90f);
  assert(stats.mean_margin > 0.90f);

  cfg.base_hz += 18.0f;
  RawFrame wrong{};
  DemodStats wrong_stats{};
  assert(!demodulate_tones(pcm.data(), pcm.size(), Submode::normal, cfg, &wrong, &wrong_stats));

  std::puts("JS8 candidate demod tests: PASS");
  return 0;
}
