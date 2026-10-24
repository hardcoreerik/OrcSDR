#include "shortwave_ssb.hpp"

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define EXT_RAM_BSS_ATTR
#endif

namespace orcsdr::shortwave_ssb {
namespace {

bool within(uint32_t hz, uint32_t lo, uint32_t hi) { return hz >= lo && hz <= hi; }

// Zero-initialized shared control block; kept out of the scarce internal RAM pool.
EXT_RAM_BSS_ATTR Control g_control;
EXT_RAM_BSS_ATTR Demod g_demod;

}  // namespace

Mode suggest(uint32_t hz) {
  // 60 m channels are USB by convention even though they sit below 10 MHz.
  if (within(hz, 5330500, 5406500)) return Mode::usb;
  // Amateur voice below 10 MHz: lower sideband.
  if (within(hz, 1800000, 2000000) || within(hz, 3500000, 4000000) || within(hz, 7000000, 7300000))
    return Mode::lsb;
  // Amateur voice above 10 MHz: upper sideband.
  if (within(hz, 10100000, 10150000) || within(hz, 14000000, 14350000) || within(hz, 18068000, 18168000) ||
      within(hz, 21000000, 21450000) || within(hz, 24890000, 24990000) || within(hz, 28000000, 29700000))
    return Mode::usb;
  return Mode::am;
}

Mode resolve(Choice choice, uint32_t hz) {
  switch (choice) {
    case Choice::am: return Mode::am;
    case Choice::usb: return Mode::usb;
    case Choice::lsb: return Mode::lsb;
    default: return suggest(hz);
  }
}

Mode current(uint32_t hz) {
  return resolve(static_cast<Choice>(g_control.choice.load(std::memory_order_relaxed)), hz);
}

const char* mode_name(Mode mode) {
  return mode == Mode::usb ? "USB" : mode == Mode::lsb ? "LSB" : "AM";
}

Choice next_choice(Choice choice) {
  switch (choice) {
    case Choice::automatic: return Choice::am;
    case Choice::am: return Choice::usb;
    case Choice::usb: return Choice::lsb;
    default: return Choice::automatic;
  }
}

bool spectrum_inverted(uint32_t hz) { return hz > kNyquistFoldHz; }

Mode demod_sideband(Mode mode, uint32_t hz) {
  if (mode == Mode::am || !spectrum_inverted(hz)) return mode;
  return mode == Mode::usb ? Mode::lsb : Mode::usb;
}

int32_t clamp_clarifier(int32_t hz) {
  return hz < -kClarifierLimitHz ? -kClarifierLimitHz : hz > kClarifierLimitHz ? kClarifierLimitHz : hz;
}

Control& control() { return g_control; }
Demod& demod_state() { return g_demod; }

void design_lowpass(Biquad (&sections)[Demod::kSections], float cutoff_hz, float rate_hz) {
  constexpr float kPi = 3.14159265358979323846f;
  const float w0 = 2.0f * kPi * cutoff_hz / rate_hz;
  const float cw = cosf(w0), sw = sinf(w0);
  for (int k = 0; k < Demod::kSections; ++k) {
    // Butterworth pole Q for an 8th-order filter: 1 / (2 sin((2k+1) pi / 16)).
    const float q = 1.0f / (2.0f * sinf((2.0f * static_cast<float>(k) + 1.0f) * kPi / 16.0f));
    const float alpha = sw / (2.0f * q);
    const float a0 = 1.0f + alpha;
    Biquad& b = sections[k];
    b.b0 = (1.0f - cw) * 0.5f / a0;
    b.b1 = (1.0f - cw) / a0;
    b.b2 = b.b0;
    b.a1 = -2.0f * cw / a0;
    b.a2 = (1.0f - alpha) / a0;
  }
}

void reset(Demod& demod) { demod = Demod{}; }

}  // namespace orcsdr::shortwave_ssb
