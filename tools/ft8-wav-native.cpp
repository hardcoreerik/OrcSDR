// Host-only: decode a 15 s (FT8) or 7.5 s (FT4) 12 kHz mono recording through the production Backend (the code the firmware runs).
//   ft8-wav-native <wav> <ft8|ft4> [--k N] [--gate N] [--fine 4|8]
#include "ft8_native_backend.hpp"
#include "ft8_wav_common.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
uint64_t now_us() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <wav> <ft8|ft4> [--k N] [--gate N] [--fine 4|8]\n", argv[0]);
    return 2;
  }
  orcsdr::ftx::Mode mode = std::strcmp(argv[2], "ft4") == 0 ? orcsdr::ftx::Mode::ft4 : orcsdr::ftx::Mode::ft8;
  orcsdr::ftx::native::Config config;
  config.now_us = now_us;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--k" && i + 1 < argc) config.candidate_k = static_cast<uint16_t>(std::atoi(argv[++i]));
    else if (a == "--gate" && i + 1 < argc) config.gate = static_cast<uint16_t>(std::atoi(argv[++i]));
    else if (a == "--fine" && i + 1 < argc) config.fine_rows = static_cast<uint8_t>(std::atoi(argv[++i]));
    else return 2;
  }
  ft8_wav_tools::Wav wav{};
  if (!ft8_wav_tools::read_wav(argv[1], &wav)) { std::fprintf(stderr, "unsupported WAV\n"); return 2; }
  orcsdr::ftx::native::Backend backend;
  if (!backend.begin(12000, mode, config)) { std::fprintf(stderr, "backend begin failed\n"); return 1; }
  backend.begin_slot(1791440160000ull);
  backend.offer_audio(wav.samples.data(), wav.samples.size());
  orcsdr::ft8::Decode out[32];
  const size_t n = backend.finish_slot(out, 32);
  const auto& s = backend.stats();
  std::printf("NATIVE decodes=%zu total=%ums spectral=%u search=%u refine=%u gates=%u coarse=%u attempted=%u\n", n, s.total_ms, s.spectral_ms,
              s.search_ms, s.refine_ms, s.gate_ms, s.coarse_candidates, s.attempted);
  for (size_t i = 0; i < n; ++i)
    std::printf("DECODE %-26s %5u Hz  dt %+5d ms  snr %s%d  call=%s grid=%s\n", out[i].message, out[i].audio_hz, out[i].dt_ms,
                (out[i].flags & orcsdr::ft8::decode_flag_snr_unavailable) ? "n/a " : "", static_cast<int>(out[i].snr_db), out[i].callsign, out[i].grid);
  return 0;
}
