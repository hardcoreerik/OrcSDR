#include "js8_native_backend.hpp"
#include "js8_frontend.hpp"
#include "js8_spectral.hpp"
#include "js8_codec.hpp"
#include "js8_ldpc_graph.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
std::vector<int16_t> make_slot(float base_hz, float amplitude, float noise_amplitude, bool with_signal, size_t start = 6000,
                             const std::array<uint8_t, kChannelSymbols>& frame = kFrame) {
  const Profile& p = profile(Submode::normal);
  std::vector<int16_t> pcm(static_cast<size_t>(p.slot_ms) * 12u, 0);
  float phase = 0.0f;
  for (size_t symbol = 0; symbol < frame.size(); ++symbol) {
    const float step = 2.0f * kPi * (base_hz + frame[symbol] * 6.25f) / 12000.0f;
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

  // A station between two 6.25 Hz search bins (real stations sit at arbitrary frequencies; the 2604 Hz station of the real 40 m capture has its
  // lowest tone at 2602.5 Hz): refinement must still recover every tone. Without refinement the coarse grid point loses most of the energy.
  for (float off_grid : {1015.6f, 1013.0f, 1017.4f}) {
    const auto offset_slot = make_slot(off_grid, 6000.0f, 2500.0f, true);
    assert(backend.begin_slot(1791440220000ull));
    for (size_t offset = 0; offset < offset_slot.size(); offset += 4096)
      assert(backend.offer_audio(offset_slot.data() + offset, std::min<size_t>(4096, offset_slot.size() - offset)));
    assert(backend.finish_slot(out, 4) == 0);
    assert(backend.raw_count() >= 1);
    const native::RawResult* r = backend.raw(0);
    assert(std::fabs(r->audio_hz - off_grid) <= 1.7f);
    for (size_t i = 0; i < kFrame.size(); ++i) assert(r->frame.tones[i] == kFrame[i]);
    assert(backend.stats().refined >= 1);
  }

  // Alias resolution: the three Normal sync blocks are identical, so an alignment one sync period (36 symbols) away from a stronger frame is dropped;
  // different frequencies and unrelated start times are kept.
  {
    const Profile& p = profile(Submode::normal);
    const size_t period = static_cast<size_t>(p.symbol_samples) * 36u;
    frontend::AliasItem items[5] = {
        {637.5f, 112320, 0.56f, 21},               // the real frame
        {637.5f, 112320 - period, 0.30f, 14},      // one period early (weaker): dropped
        {637.5f, 112320 + period, 0.35f, 14},      // one period late (weaker): dropped
        {487.5f, 112800, 0.48f, 21},               // another station: kept
        {637.5f, 112320 + 50000, 0.40f, 15},       // same frequency, unrelated start: kept
    };
    bool drop[5];
    frontend::mark_aliases(p, items, 5, drop);
    assert(!drop[0] && drop[1] && drop[2] && !drop[3] && !drop[4]);
    // a K7YXZ-style pair: equal sync hits, the aliased one has the weaker frame energy
    frontend::AliasItem pair[2] = {{837.5f, 61920, 0.46f, 14}, {837.5f, 61920 + period, 0.69f, 14}};
    frontend::mark_aliases(p, pair, 2, drop);
    assert(drop[0] && !drop[1]);
  }

  // The latest start a whole frame can have in a 15 s slot (start row 29 of 186, sample 27840, dt +1820 ms) used to be skipped: the backend counted a full extra symbol of
  // rows (158) where the sync search needs 157. Such a frame must be found, with its tones intact.
  {
    const auto late = make_slot(base_hz, 6000.0f, 2500.0f, true, 27840);
    assert(late.size() == 180000 && 27840 + kFrame.size() * 1920 <= late.size());
    assert(backend.begin_slot(1791440220000ull));
    for (size_t offset = 0; offset < late.size(); offset += 4096)
      assert(backend.offer_audio(late.data() + offset, std::min<size_t>(4096, late.size() - offset)));
    assert(backend.finish_slot(out, 4) == 0);
    assert(backend.raw_count() >= 1);
    const native::RawResult* last = backend.raw(0);
    assert(last != nullptr && std::fabs(last->audio_hz - base_hz) <= 3.2f && std::abs(last->dt_ms - 1820) <= 45);
    for (size_t i = 0; i < kFrame.size(); ++i) assert(last->frame.tones[i] == kFrame[i]);
  }

  // Exactly one frame produces 157 grid rows; one additional half symbol produces 158.
  // Both must search row zero, rather than requiring a spare row beyond the frame.
  for (size_t extra : {size_t{0}, size_t{960}}) {
    auto exact = make_slot(base_hz, 6000.0f, 0.0f, true, 0);
    exact.resize(kFrame.size() * 1920 + extra);
    assert(backend.begin_slot(1791440220000ull));
    assert(backend.offer_audio(exact.data(), exact.size()));
    (void)backend.finish_slot(out, 4);
    assert(backend.grid_rows() == 157 + extra / 960);
    assert(backend.raw_count() >= 1);
    const auto* first = backend.raw(0);
    for (size_t i = 0; i < kFrame.size(); ++i) assert(first->frame.tones[i] == kFrame[i]);
  }

  // Nine complete, clean, distinct messages in one slot: a busy slot exceeds the old runtime output capacity of eight.
  {
    std::vector<int16_t> busy(180000, 0);
    for (uint8_t report = 1; report <= 9; ++report) {
      uint8_t info[87]{}, cw[174]{};
      const char* text = "UvnVIpm34Fqg"; // established WO7I directed frame, with a distinct SNR report below
      for (size_t c = 0; c < 12; ++c) {
        size_t value = 0;
        while (value < 64 && codec::kAlphabet[value] != text[c]) ++value;
        assert(value < 64);
        for (size_t b = 0; b < 6; ++b) info[c * 6 + b] = (value >> (5 - b)) & 1u;
      }
      for (size_t b = 0; b < 6; ++b) info[66 + b] = (report >> (5 - b)) & 1u;
      info[73] = info[74] = 1;
      const uint16_t crc = codec::crc12(info);
      for (size_t b = 0; b < 12; ++b) info[75 + b] = (crc >> (11 - b)) & 1u;
      std::copy(info, info + 87, cw + 87);
      for (size_t check = 0; check < 87; ++check)
        for (size_t e = ldpc_graph::kCheckOffsets[check]; e < ldpc_graph::kCheckOffsets[check + 1]; ++e) {
          const size_t v = ldpc_graph::kCheckVariables[e];
          if (v >= 87) cw[check] ^= cw[v];
        }
      auto tones = kFrame;
      for (size_t s = 0; s < 58; ++s)
        tones[codec::data_tone_position(s)] = (cw[3*s] << 2) | (cw[3*s+1] << 1) | cw[3*s+2];
      const auto signal = make_slot(400.0f + report * 125.0f, 1600.0f, 0.0f, true, 6000, tones);
      for (size_t n = 0; n < busy.size(); ++n) busy[n] = static_cast<int16_t>(busy[n] + signal[n]);
    }
    assert(backend.begin_slot(1791440220000ull));
    assert(backend.offer_audio(busy.data(), busy.size()));
    orcsdr::ft8::Decode messages[24];
    assert(backend.finish_slot(messages, 24) == 9);
    for (size_t n = 0; n < 9; ++n) assert(std::strcmp(messages[n].callsign, "WO7I") == 0);
  }

  std::printf("js8_native_backend_tests: PASS (grid vs oracle worst %.2e of peak, raw frames %u, candidates %u)\n",
              static_cast<double>(worst / peak), static_cast<unsigned>(stats.raw_frames), static_cast<unsigned>(stats.candidates));
  return 0;
}
