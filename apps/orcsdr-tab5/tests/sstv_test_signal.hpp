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
  double time_scale = 1.0;   // >1 stretches every tone (transmitter clock slower)
  double luma_hz = 1900.;    // neutral luminance tone used by the image rows
  double tone_offset_hz = 0; // added to every tone (a mistuned SSB dial shifts all audio by the same amount)
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
    const auto samples = static_cast<int>(std::lround(kSampleRate * ms * time_scale / 1000.));
    const double delta = (2.0 * kPi * (hz + tone_offset_hz)) / kSampleRate;
    for (int i = 0; i < samples; ++i) {
      push(static_cast<int16_t>(std::lround(amplitude * std::sin(phase))));
      phase += delta;
      if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
    }
  }
  // Drift-free tone for pixel-level signals: segment boundaries come from a cumulative exact
  // clock, so 320 pixels of 0.275 ms add up to exactly 88 ms instead of accumulating rounding.
  double exact_ms = 0;
  long exact_emitted = 0;
  void exact_reset() { exact_ms = 0; exact_emitted = 0; }
  void exact_tone(double hz, double ms) {
    exact_ms += ms * time_scale;
    const long target = std::lround(exact_ms * kSampleRate / 1000.);
    const long samples = target - exact_emitted;
    exact_emitted = target;
    const double delta = (2.0 * kPi * (hz + tone_offset_hz)) / kSampleRate;
    for (long i = 0; i < samples; ++i) {
      push(static_cast<int16_t>(std::lround(amplitude * std::sin(phase))));
      phase += delta;
      if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
    }
  }
  static double luma_to_hz(uint8_t v) { return 1500.0 + 800.0 * v / 255.0; }
  // Robot 36 from a 320x240 grayscale image (luma only; chroma neutral).
  void robot36_image(const uint8_t* gray) {
    exact_reset();
    for (unsigned row = 0; row < 240; ++row) {
      exact_tone(1200, 9);
      exact_tone(1500, 3);
      for (unsigned x = 0; x < 320; ++x) exact_tone(luma_to_hz(gray[row * 320u + x]), 0.275);
      exact_tone((row & 1u) ? 2300 : 1500, 4.5);
      exact_tone(1900, 1.5);
      exact_tone(1900, 44);
    }
    tone(1900, 50);
  }
  // PD120 (640x496) from the same 320x240 picture, scaled 2x horizontally and to 496 rows.
  void pd120_image(const uint8_t* gray) {
    exact_reset();
    for (unsigned pair = 0; pair < 248; ++pair) {
      exact_tone(1200, 20);
      exact_tone(1500, 2.08);
      for (unsigned part = 0; part < 4; ++part) {
        const bool luma = part == 0 || part == 3;
        const unsigned row = pair * 2u + (part == 3 ? 1u : 0u);
        const unsigned src_row = row * 240u / 496u;
        for (unsigned x = 0; x < 640; ++x)
          exact_tone(luma ? luma_to_hz(gray[src_row * 320u + x / 2u]) : 1900.0, 0.19);
      }
    }
    tone(1900, 50);
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
        tone(luma_hz, 88);
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
