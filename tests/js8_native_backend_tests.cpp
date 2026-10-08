#include "js8_native_backend.hpp"
#include "js8_spectral.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
using namespace orcsdr::js8;
constexpr float kPi = 3.14159265358979323846f;

// A full Normal frame as it is used by the demodulator tests: the three established sync blocks plus data tones.
constexpr std::array<uint8_t, kChannelSymbols> kFrame{{
    4,2,5,6,1,3,0, 1,0,2,6,6,3,1,6,6,4,0,1,7,0,7,2,6,2,6,0,4,3,4,5,2,3,5,2,0,
    4,2,5,6,1,3,0, 3,4,2,5,4,5,7,0,1,6,3,6,7,0,2,3,5,6,4,5,7,4,0,0,1,7,3,6,4,
    4,2,5,6,1,3,0}};

uint32_t noise_state = 0x2468ace1u;
float noise() {
  noise_state ^= noise_state << 13;
  noise_state ^= noise_state >> 17;
  noise_state ^= noise_state << 5;
  return static_cast<float>(static_cast<int32_t>(noise_state & 0xffffu) - 32768) / 32768.0f;
}

// 15 s slot with the frame 0.5 s in, tone 0 at base_hz, and uniform noise of the given amplitude.
std::vector<int16_t> make_slot(float base_hz, float amplitude, float noise_amplitude, bool with_signal) {
  const Profile& p = profile(Submode::normal);
  std::vector<int16_t> pcm(static_cast<size_t>(p.slot_ms) * 12u, 0);
  const size_t start = 6000;   // 0.5 s
  float phase = 0.0f;
  for (size_t symbol = 0; symbol < kFrame.size(); ++symbol) {
    const float step = 2.0f * kPi * (base_hz + kFrame[symbol] * 6.25f) / 12000.0f;
    for (size_t n = 0; n < p.symbol_samples; ++n) {
      float sample = noise_amplitude * noise();
      if (with_signal) sample += amplitude * std::sin(phase);
      pcm[start + symbol * p.symbol_samples + n] = static_cast<int16_t>(sample);
      phase += step;
      if (phase > 2.0f * kPi) phase -= 2.0f * kPi;
    }
  }
  if (!with_signal)
    for (size_t i = 0; i < start; ++i) pcm[i] = static_cast<int16_t>(noise_amplitude * noise());
  return pcm;
}
}  // namespace

int main() {
  assert(native::self_check());

  // Only Normal is established: every other submode is refused, and the backend never falls back to Normal.
  native::Backend backend;
  assert(!backend.begin(12000, Submode::fast, native::Config{}));
  assert(!backend.begin(8000, Submode::normal, native::Config{}));
  assert(backend.begin(12000, Submode::normal, native::Config{}));
  assert(!backend.set_submode(Submode::fast));
  assert(!backend.set_submode(Submode::slow));
  assert(backend.submode() == Submode::normal);

  orcsdr::ft8::Decode out[4];

  // A slot with no audio, or a slot flagged incomplete, produces nothing and says why.
  assert(backend.begin_slot(1791440160000ull));
  assert(backend.finish_slot(out, 4) == 0 && backend.stats().slot_too_short);
  assert(backend.begin_slot(1791440175000ull));
  const auto quiet = make_slot(900.0f, 0.0f, 0.0f, false);
  assert(backend.offer_audio(quiet.data(), quiet.size()));
  assert(backend.finish_slot(out, 4, true) == 0 && backend.stats().incomplete);

  // Noise only: no raw frame and, of course, no decode.
  const auto noise_only = make_slot(900.0f, 0.0f, 2500.0f, false);
  assert(backend.begin_slot(1791440190000ull));
  assert(backend.offer_audio(noise_only.data(), noise_only.size()));
  assert(backend.finish_slot(out, 4) == 0);
  assert(backend.stats().raw_frames == 0);
  assert(backend.stats().decodes == 0);

  // A synthetic Normal frame: the FFT grid agrees with the exact-correlation oracle, the sync search finds the frame, and the
  // demodulator recovers the raw tones at the right frequency and time. Still zero Decode records.
  const float base_hz = 1012.5f;
  const auto slot = make_slot(base_hz, 6000.0f, 2500.0f, true);
  assert(backend.begin_slot(1791440205000ull));
  for (size_t offset = 0; offset < slot.size(); offset += 4096)   // offered in chunks, as the runtime does
    assert(backend.offer_audio(slot.data() + offset, std::min<size_t>(4096, slot.size() - offset)));
  assert(backend.finish_slot(out, 4) == 0);
  const auto& stats = backend.stats();
  assert(stats.decodes == 0);
  assert(stats.grid_rows > 150 && stats.candidates >= 1 && stats.best_sync_score > 0.3f);
  assert(backend.raw_count() >= 1);
  const native::RawResult* best = backend.raw(0);
  assert(best != nullptr && std::fabs(best->audio_hz - base_hz) <= 3.2f);
  assert(std::abs(best->dt_ms) <= 45);
  for (size_t i = 0; i < kFrame.size(); ++i) assert(best->frame.tones[i] == kFrame[i]);

  // The FFT grid equals the oracle on identical PCM (relative to the strongest cell).
  std::vector<float> oracle(backend.grid_rows() * backend.grid_bins());
  spectral::Config config{};
  spectral::OutputGrid output{oracle.data(), backend.grid_rows(), backend.grid_bins()};
  const size_t oracle_rows = spectral::power_grid(slot.data(), slot.size(), Submode::normal, config, output);
  assert(oracle_rows == backend.grid_rows());
  float peak = 0.0f, worst = 0.0f;
  for (size_t i = 0; i < oracle.size(); ++i) peak = std::max(peak, oracle[i]);
  for (size_t i = 0; i < oracle.size(); ++i) worst = std::max(worst, std::fabs(oracle[i] - backend.grid()[i]));
  assert(worst <= peak * 1.0e-4f);

  std::printf("js8_native_backend_tests: PASS (grid vs oracle worst %.2e of peak, raw frames %u, candidates %u)\n",
              static_cast<double>(worst / peak), static_cast<unsigned>(stats.raw_frames), static_cast<unsigned>(stats.candidates));
  return 0;
}
