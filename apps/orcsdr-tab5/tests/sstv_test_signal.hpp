// Shared synthetic SSTV audio for host regressions (48 kHz mono PCM16).
// Every generator writes straight into the production audio tap
// (offer_audio_48k) and, unless disabled, services the decoder after every
// 480-sample block, exactly as the firmware's UI loop would.
#pragma once
#include "sstv_receiver.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace sstv_test {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSampleRate = 48000;

inline void require(bool pass, const char* why) {
  if (!pass) {
    std::fprintf(stderr, "SSTV FAIL: %s\n", why);
    std::exit(1);
  }
}

struct Signal {
  std::array<int16_t, 480> pcm{};
  size_t pending = 0;
  double phase = 0;
  double amplitude = 11000.;
  // false: audio is offered but the decoder is not serviced, so the 4096
  // sample ring fills and the producer must drop samples (overrun).
  bool service_enabled = true;
  uint32_t rng = 0x1234567u;

  void flush() {
    if (!pending) return;
    orcsdr::sstv::offer_audio_48k(pcm.data(), pending);
    if (service_enabled) orcsdr::sstv::service(16000);
    pending = 0;
  }
  void push(int16_t v) {
    pcm[pending++] = v;
    if (pending == pcm.size()) flush();
  }
  void tone(double hz, double ms) {
    const auto samples = static_cast<int>(std::lround(kSampleRate * ms / 1000.));
    const double delta = (2.0 * kPi * hz) / kSampleRate;
    for (int i = 0; i < samples; ++i) {
      push(static_cast<int16_t>(std::lround(amplitude * std::sin(phase))));
      phase += delta;
      if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
    }
  }
  void silence(double ms) {
    const auto samples = static_cast<int>(std::lround(kSampleRate * ms / 1000.));
    for (int i = 0; i < samples; ++i) push(0);
  }
  // Deterministic white noise at a given peak amplitude.
  void noise(double ms, double peak) {
    const auto samples = static_cast<int>(std::lround(kSampleRate * ms / 1000.));
    for (int i = 0; i < samples; ++i) {
      rng = rng * 1664525u + 1013904223u;
      const double u = static_cast<double>(rng >> 8) / 8388608.0 - 1.0;
      push(static_cast<int16_t>(std::lround(peak * u)));
    }
  }
  void preamble() {
    tone(1900, 300);
    tone(1200, 10);
    tone(1900, 300);
  }
  void vis(uint8_t code, bool flip_parity = false) {
    tone(1200, 30);
    unsigned ones = 0;
    for (unsigned i = 0; i < 7; ++i) {
      const bool bit = (code >> i) & 1u;
      if (bit) ++ones;
      tone(bit ? 1100 : 1300, 30);
    }
    tone(((ones & 1u) != 0) ^ flip_parity ? 1100 : 1300, 30);
    tone(1200, 30);
  }
  void header(uint8_t code) {
    preamble();
    vis(code);
  }
  // Robot 36 rows [first, first+count). Row 100 carries the 1 ms stripe test.
  void robot36_rows(unsigned first = 0, unsigned count = 240) {
    for (unsigned row = first; row < first + count && row < 240; ++row) {
      tone(1200, 9);
      tone(1500, 3);
      if (row == 100) {
        for (unsigned stripe = 0; stripe < 88; ++stripe) tone((stripe & 1u) ? 2300 : 1500, 1);
      } else {
        tone(1900, 88);
      }
      tone((row & 1u) ? 2300 : 1500, 4.5);
      tone(1900, 1.5);
      tone(1900, 44);
    }
  }
  void pd120_pairs(unsigned first = 0, unsigned count = 248) {
    for (unsigned pair = first; pair < first + count && pair < 248; ++pair) {
      tone(1200, 20);
      tone(1500, 2.08);
      for (unsigned channel = 0; channel < 4; ++channel) tone(1900, 121.60);
    }
  }
  // Scottie S1 (the extra 9 ms pre-sync after VIS is sent by the caller).
  void scottie_s1_rows(unsigned first = 0, unsigned count = 256) {
    for (unsigned row = first; row < first + count && row < 256; ++row) {
      tone(1500, 1.5);
      tone(1900, 138.24);
      tone(1500, 1.5);
      tone(1900, 138.24);
      tone(1200, 9);
      tone(1500, 1.5);
      tone(1900, 138.24);
    }
  }
};

}  // namespace sstv_test
