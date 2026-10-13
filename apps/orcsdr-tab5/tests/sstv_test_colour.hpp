// Colour SSTV transmitter model for host regressions.
//
// Deliberately independent of the decoder: segment order and timings are written out here from the published
// mode descriptions (Martin / Scottie / Robot / PD), not read from the receiver's tables, so a layout error in
// either side shows up as a failing test instead of cancelling itself out.
//
// Colour model: YCrCb with R = Y + 1.40 Cr', B = Y + 1.78 Cb' (primes: offset by 128), the inverse of the
// decoder's integer formula. RGB modes send the channels directly.
#pragma once
#include "sstv_test_signal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sstv_test {

struct ColourMode {
  orcsdr::sstv::Mode mode;
  uint8_t vis;
  unsigned width, height;
  double dwell_ms;      // time per pixel of one full-width channel
  const char* name;
};

// Order matches the mode list in the receiver.
inline const ColourMode* colour_modes(size_t* count) {
  using orcsdr::sstv::Mode;
  static const ColourMode table[] = {
      {Mode::robot36, 8, 320, 240, 0.275, "ROBOT 36"},      {Mode::robot72, 12, 320, 240, 0.43125, "ROBOT 72"},
      {Mode::martin_m1, 44, 320, 256, 0.4576, "MARTIN M1"}, {Mode::martin_m2, 40, 320, 256, 0.2288, "MARTIN M2"},
      {Mode::scottie_s1, 60, 320, 256, 0.432, "SCOTTIE S1"}, {Mode::scottie_s2, 56, 320, 256, 0.2752, "SCOTTIE S2"},
      {Mode::scottie_dx, 76, 320, 256, 1.08, "SCOTTIE DX"}, {Mode::pd50, 93, 320, 256, 0.286, "PD 50"},
      {Mode::pd90, 99, 320, 256, 0.532, "PD 90"},           {Mode::pd120, 95, 640, 496, 0.19, "PD 120"},
      {Mode::pd160, 98, 512, 400, 0.382, "PD 160"},         {Mode::pd180, 96, 640, 496, 0.286, "PD 180"},
      {Mode::pd240, 97, 640, 496, 0.382, "PD 240"},         {Mode::pd290, 94, 800, 616, 0.286, "PD 290"},
  };
  *count = sizeof(table) / sizeof(table[0]);
  return table;
}

struct Rgb { uint8_t r, g, b; };

// Source pixel for a mode-sized grid: nearest neighbour from the 320x240 reference.
inline Rgb source_at(const uint8_t* rgb320x240, unsigned x, unsigned y, unsigned w, unsigned h) {
  const unsigned sx = std::min(319u, x * 320u / w), sy = std::min(239u, y * 240u / h);
  const uint8_t* p = rgb320x240 + (static_cast<size_t>(sy) * 320u + sx) * 3u;
  return {p[0], p[1], p[2]};
}

inline double clamp255(double v) { return std::min(255.0, std::max(0.0, v)); }
inline double luma_of(Rgb c) { return clamp255(0.299 * c.r + 0.587 * c.g + 0.114 * c.b); }
inline double cr_of(Rgb c) { return clamp255((c.r - luma_of(c)) / 1.40 + 128.0); }
inline double cb_of(Rgb c) { return clamp255((c.b - luma_of(c)) / 1.78 + 128.0); }
inline uint8_t byte_of(double v) { return static_cast<uint8_t>(std::lround(clamp255(v))); }

// Sends one complete picture (header and image rows, no trailing silence).
inline void send_colour(Signal& s, const ColourMode& m, const uint8_t* rgb) {
  using orcsdr::sstv::Mode;
  s.header(m.vis);
  s.exact_reset();
  auto px = [&](double v) { return Signal::luma_to_hz(byte_of(v)); };
  auto src = [&](unsigned x, unsigned y) { return source_at(rgb, x, y, m.width, m.height); };
  auto channel = [&](unsigned y, char which) {
    for (unsigned x = 0; x < m.width; ++x) {
      const Rgb c = src(x, y);
      s.exact_tone(px(which == 'r' ? c.r : which == 'g' ? c.g : c.b), m.dwell_ms);
    }
  };
  // Chroma over a group of rows, averaged over `across` neighbouring columns: returns one value per output pixel.
  auto chroma = [&](unsigned y0, unsigned rows, unsigned across, unsigned out_x, bool red) {
    double sum = 0;
    unsigned n = 0;
    for (unsigned dy = 0; dy < rows; ++dy)
      for (unsigned dx = 0; dx < across; ++dx) {
        const Rgb c = src(std::min(m.width - 1, out_x * across + dx), std::min(m.height - 1, y0 + dy));
        sum += red ? cr_of(c) : cb_of(c);
        ++n;
      }
    return sum / n;
  };
  switch (m.mode) {
    case Mode::robot36:
      for (unsigned y = 0; y < m.height; ++y) {
        s.exact_tone(1200, 9);
        s.exact_tone(1500, 3);
        for (unsigned x = 0; x < m.width; ++x) s.exact_tone(px(luma_of(src(x, y))), m.dwell_ms);
        const bool red = (y & 1u) == 0;                 // R-Y on even lines, B-Y on odd, from the line pair
        s.exact_tone(red ? 1500 : 2300, 4.5);
        s.exact_tone(1900, 1.5);
        for (unsigned x = 0; x < m.width / 2; ++x) s.exact_tone(px(chroma(y & ~1u, 2, 2, x, red)), m.dwell_ms);
      }
      break;
    case Mode::robot72:
      for (unsigned y = 0; y < m.height; ++y) {
        s.exact_tone(1200, 9);
        s.exact_tone(1500, 3);
        for (unsigned x = 0; x < m.width; ++x) s.exact_tone(px(luma_of(src(x, y))), m.dwell_ms);
        s.exact_tone(1500, 4.5);
        s.exact_tone(1900, 1.5);
        for (unsigned x = 0; x < m.width / 2; ++x) s.exact_tone(px(chroma(y, 1, 2, x, true)), m.dwell_ms);
        s.exact_tone(2300, 4.5);
        s.exact_tone(1900, 1.5);
        for (unsigned x = 0; x < m.width / 2; ++x) s.exact_tone(px(chroma(y, 1, 2, x, false)), m.dwell_ms);
      }
      break;
    case Mode::martin_m1:
    case Mode::martin_m2:
      for (unsigned y = 0; y < m.height; ++y) {
        s.exact_tone(1200, 4.862);
        s.exact_tone(1500, 0.572);
        channel(y, 'g');
        s.exact_tone(1500, 0.572);
        channel(y, 'b');
        s.exact_tone(1500, 0.572);
        channel(y, 'r');
        s.exact_tone(1500, 0.572);
      }
      break;
    case Mode::scottie_s1:
    case Mode::scottie_s2:
    case Mode::scottie_dx:
      s.exact_tone(1200, 9);                            // the extra start sync after the VIS code
      for (unsigned y = 0; y < m.height; ++y) {
        s.exact_tone(1500, 1.5);
        channel(y, 'g');
        s.exact_tone(1500, 1.5);
        channel(y, 'b');
        s.exact_tone(1200, 9);                          // sync sits between B and R
        s.exact_tone(1500, 1.5);
        channel(y, 'r');
      }
      break;
    default:                                            // PD: Y(even row), R-Y, B-Y, Y(odd row), chroma shared
      for (unsigned pair = 0; pair < m.height / 2; ++pair) {
        const unsigned y0 = pair * 2u;
        s.exact_tone(1200, 20);
        s.exact_tone(1500, 2.08);
        for (unsigned x = 0; x < m.width; ++x) s.exact_tone(px(luma_of(src(x, y0))), m.dwell_ms);
        for (unsigned x = 0; x < m.width; ++x) s.exact_tone(px(chroma(y0, 2, 1, x, true)), m.dwell_ms);
        for (unsigned x = 0; x < m.width; ++x) s.exact_tone(px(chroma(y0, 2, 1, x, false)), m.dwell_ms);
        for (unsigned x = 0; x < m.width; ++x) s.exact_tone(px(luma_of(src(x, y0 + 1))), m.dwell_ms);
      }
      break;
  }
  s.tone(1900, 50);
}

}  // namespace sstv_test
