#pragma once

#include <algorithm>

#include <M5GFX.h>

// M5GFX supplies these native ESP-IDF timing helpers.  Keep the legacy call
// sites source-compatible without pulling the Arduino component back in.
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
