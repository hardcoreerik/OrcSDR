#pragma once
// Equivalent of apps/orcsdr-tab5/ui/orcsdr_m5_time.hpp for the shim build. The Tab5 version includes
// the real <M5GFX.h>, whose global M5Canvas would clash with the scaled one, so this pulls in only
// the LovyanGFX pieces the scaled layer already uses. Force-included into C++ sources (-include).
#include <algorithm>
#include "orcsdr_gfx/scaled_gfx.hpp"

using lgfx::delay;
using lgfx::micros;
using lgfx::millis;
using std::max;
using std::min;

inline constexpr double DEG_TO_RAD = 0.01745329251994329576923690768489;

template <typename T, typename Lower, typename Upper>
constexpr T constrain(T value, Lower lower, Upper upper) {
  return std::clamp(value, static_cast<T>(lower), static_cast<T>(upper));
}
