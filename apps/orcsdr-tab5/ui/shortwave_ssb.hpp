#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Sideband (USB/LSB) listening for the Shortwave Explorer. Pure logic and DSP with no dependency on main.cpp:
// the receiver supplies complex 8-bit IQ at 960 kHz (decimation 4 to 240 kHz, then 5 to 48 kHz audio) and takes
// float audio back; the caller applies its own AGC and limiter.
namespace orcsdr::shortwave_ssb {

enum class Mode : uint8_t { am = 0, usb = 1, lsb = 2 };
// What the user chose: auto follows the band plan, the rest pin a mode.
enum class Choice : uint8_t { automatic = 0, am = 1, usb = 2, lsb = 3 };

constexpr int32_t kClarifierLimitHz = 1500;
constexpr int32_t kClarifierStepHz = 50;
constexpr uint32_t kPassbandHz = 2700;   // fixed voice filter width in SSB
constexpr float kAudioLowHz = 300.0f;    // lower edge of the voice passband
// The RTL2832 samples HF at 28.8 MHz; above half of that the direct-sampling path folds the spectrum back
// inverted, which swaps the sidebands.
constexpr uint32_t kNyquistFoldHz = 14400000u;

// Mode the band plan suggests for this frequency. AM everywhere except the amateur voice segments.
Mode suggest(uint32_t frequency_hz);
// The mode in force: the pinned choice, or the suggestion.
Mode resolve(Choice choice, uint32_t frequency_hz);
// The mode in force for the pin held in control() at this frequency.
Mode current(uint32_t frequency_hz);
const char* mode_name(Mode mode);
Choice next_choice(Choice choice);
// True when the receiver delivers the spectrum mirrored at this frequency, so USB must be demodulated as LSB.
bool spectrum_inverted(uint32_t frequency_hz);
// The sideband to demodulate for a dial mode at a frequency, after the fold correction. Mode::am passes through.
Mode demod_sideband(Mode mode, uint32_t frequency_hz);
int32_t clamp_clarifier(int32_t hz);

// Shared control state. Zero-initialized storage is valid (auto, no clarifier), so one instance can live in PSRAM.
struct Control {
  std::atomic<uint8_t> choice;
  std::atomic<int32_t> clarifier_hz;
};
Control& control();
// The receiver's one demodulator state; zero-initialized, lives in PSRAM.
struct Demod;
Demod& demod_state();

// One biquad low-pass section on a complex signal (same real coefficients on I and Q).
struct Biquad {
  float b0, b1, b2, a1, a2;
};

struct Demod {
  static constexpr int kSections = 4;   // 8-pole Butterworth
  float ci = 0, si = 0;                 // front oscillator phasor (all zero = not started, treated as 1+0j)
  float cb = 0, sb = 0;                 // back oscillator phasor
  float z_i[kSections][2]{};
  float z_q[kSections][2]{};
  float iq_i = 0, iq_q = 0;             // 960 kHz pre-filter state
  float sum_i = 0, sum_q = 0;           // 4:1 accumulator
  float sum48_i = 0, sum48_q = 0;       // 5:1 accumulator
  uint8_t rf_phase = 0;
  uint8_t audio_phase = 0;
  uint8_t active = 0;                   // sideband this state belongs to (1 + Mode); a change restarts the filters
};

// Filter design for the 48 kHz stage; cutoff in Hz.
void design_lowpass(Biquad (&sections)[Demod::kSections], float cutoff_hz, float rate_hz);
void reset(Demod& demod);

// Demodulate a block. `emit(float)` receives one 48 kHz audio sample at a time.
// sideband: Mode::usb or Mode::lsb (already corrected for the fold). clarifier_hz shifts the pitch by the
// usual amount: raising the dial frequency lowers USB pitch and raises LSB pitch.
// Returns the number of audio samples emitted; clipped_pairs counts samples at the 8-bit rails.
template <typename Emit>
size_t process(Demod& demod, const uint8_t* iq, size_t bytes, uint8_t rf_decimation, Mode sideband,
               int32_t clarifier_hz, float rf_rate_hz, Emit&& emit, uint32_t* clipped_pairs = nullptr) {
  if (rf_decimation == 0 || (sideband != Mode::usb && sideband != Mode::lsb)) return 0;
  if (demod.active != static_cast<uint8_t>(sideband) + 1u) {
    demod = Demod{};
    demod.active = static_cast<uint8_t>(sideband) + 1u;
  }
  constexpr float kPi = 3.14159265358979323846f;
  constexpr float kAudioRate = 48000.0f;
  constexpr float kCentreHz = kAudioLowHz + kPassbandHz / 2.0f;  // sideband centre after the front shift
  Biquad sections[Demod::kSections];
  design_lowpass(sections, kPassbandHz / 2.0f + 100.0f, kAudioRate);
  // USB sits above the dial, LSB below: shift it down (USB) or up (LSB) onto the filter centre.
  const float direction = sideband == Mode::usb ? -1.0f : 1.0f;
  const float clar = static_cast<float>(sideband == Mode::usb ? clarifier_hz : -clarifier_hz);
  const float front_step = direction * 2.0f * kPi * (kCentreHz + clar) / kAudioRate;
  const float back_step = -direction * 2.0f * kPi * kCentreHz / kAudioRate;
  const float fc = cosf(front_step), fs = sinf(front_step);
  const float bc = cosf(back_step), bs = sinf(back_step);
  // 960 kHz pre-filter: a wide one-pole that keeps adjacent-band signals out of the 48 kHz fold.
  const float pre_k = 1.0f - expf(-kPi * 9000.0f / rf_rate_hz);
  const float inv_rf = 1.0f / static_cast<float>(rf_decimation);

  float ci = demod.ci, si = demod.si, cb = demod.cb, sb = demod.sb;
  if (ci == 0.0f && si == 0.0f) ci = 1.0f;
  if (cb == 0.0f && sb == 0.0f) cb = 1.0f;
  float iq_i = demod.iq_i, iq_q = demod.iq_q;
  float sum_i = demod.sum_i, sum_q = demod.sum_q;
  float s48_i = demod.sum48_i, s48_q = demod.sum48_q;
  uint8_t rf_phase = demod.rf_phase, audio_phase = demod.audio_phase;
  float zi[Demod::kSections][2], zq[Demod::kSections][2];
  for (int s = 0; s < Demod::kSections; ++s)
    for (int k = 0; k < 2; ++k) {
      zi[s][k] = demod.z_i[s][k];
      zq[s][k] = demod.z_q[s][k];
    }
  size_t count = 0;
  uint32_t clipped = 0;
  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    const int32_t i_c = static_cast<int32_t>(iq[offset]) - 128;
    const int32_t q_c = static_cast<int32_t>(iq[offset + 1]) - 128;
    clipped += (static_cast<uint32_t>(i_c + 127) > 253u) | (static_cast<uint32_t>(q_c + 127) > 253u);
    iq_i += pre_k * (static_cast<float>(i_c) - iq_i);
    iq_q += pre_k * (static_cast<float>(q_c) - iq_q);
    sum_i += iq_i;
    sum_q += iq_q;
    if (++rf_phase != rf_decimation) continue;
    rf_phase = 0;
    s48_i += sum_i * inv_rf;
    s48_q += sum_q * inv_rf;
    sum_i = 0;
    sum_q = 0;
    if (++audio_phase != 5) continue;
    audio_phase = 0;
    float i = s48_i * 0.2f, q = s48_q * 0.2f;
    s48_i = 0;
    s48_q = 0;
    // Shift the wanted sideband onto the filter centre: (i + jq)(c + js).
    float mi = i * ci - q * si;
    float mq = i * si + q * ci;
    const float nc = ci * fc - si * fs;
    si = si * fc + ci * fs;
    ci = nc;
    for (int s = 0; s < Demod::kSections; ++s) {
      const Biquad& b = sections[s];
      const float oi = b.b0 * mi + zi[s][0];
      zi[s][0] = b.b1 * mi - b.a1 * oi + zi[s][1];
      zi[s][1] = b.b2 * mi - b.a2 * oi;
      mi = oi;
      const float oq = b.b0 * mq + zq[s][0];
      zq[s][0] = b.b1 * mq - b.a1 * oq + zq[s][1];
      zq[s][1] = b.b2 * mq - b.a2 * oq;
      mq = oq;
    }
    // Shift back down to baseband audio and keep the real part.
    const float audio = mi * cb - mq * sb;
    const float nbc = cb * bc - sb * bs;
    sb = sb * bc + cb * bs;
    cb = nbc;
    emit(audio);
    ++count;
  }
  const float n1 = sqrtf(ci * ci + si * si), n2 = sqrtf(cb * cb + sb * sb);
  if (n1 > 0.5f) { ci /= n1; si /= n1; }
  if (n2 > 0.5f) { cb /= n2; sb /= n2; }
  demod.ci = ci; demod.si = si; demod.cb = cb; demod.sb = sb;
  demod.iq_i = iq_i; demod.iq_q = iq_q;
  demod.sum_i = sum_i; demod.sum_q = sum_q;
  demod.sum48_i = s48_i; demod.sum48_q = s48_q;
  demod.rf_phase = rf_phase; demod.audio_phase = audio_phase;
  for (int s = 0; s < Demod::kSections; ++s)
    for (int k = 0; k < 2; ++k) {
      demod.z_i[s][k] = zi[s][k];
      demod.z_q[s][k] = zq[s][k];
    }
  if (clipped_pairs) *clipped_pairs += clipped;
  return count;
}

}  // namespace orcsdr::shortwave_ssb
