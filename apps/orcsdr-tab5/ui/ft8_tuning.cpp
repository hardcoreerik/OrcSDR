#include "ft8_tuning.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::ft8::tuning {

const char* step_label(size_t index) {
  static const char* const kLabels[kStepCount] = {"10 Hz", "100 Hz", "1 kHz", "5 kHz", "10 kHz", "100 kHz"};
  return index < kStepCount ? kLabels[index] : "?";
}

uint32_t apply_steps(uint32_t dial_hz, int32_t detents, uint32_t step_hz) {
  int64_t value = static_cast<int64_t>(dial_hz) + static_cast<int64_t>(detents) * static_cast<int64_t>(step_hz);
  if (value < static_cast<int64_t>(kMinDialHz)) value = kMinDialHz;
  if (value > static_cast<int64_t>(kMaxDialHz)) value = kMaxDialHz;
  return static_cast<uint32_t>(value);
}

bool parse_mhz(const char* text, uint32_t* hz) {
  if (text == nullptr || hz == nullptr || text[0] == '\0') return false;
  uint64_t whole = 0;
  uint64_t fraction = 0;
  int decimals = 0;
  bool dot = false, digits = false;
  for (const char* p = text; *p; ++p) {
    if (*p == '.') {
      if (dot) return false;
      dot = true;
    } else if (*p >= '0' && *p <= '9') {
      digits = true;
      if (!dot) {
        whole = whole * 10 + static_cast<uint64_t>(*p - '0');
        if (whole > 5000) return false;   // far above any receivable frequency; stops overflow
      } else {
        if (decimals >= 6) return false;
        fraction = fraction * 10 + static_cast<uint64_t>(*p - '0');
        ++decimals;
      }
    } else {
      return false;
    }
  }
  if (!digits) return false;
  for (int i = decimals; i < 6; ++i) fraction *= 10;
  const uint64_t value = whole * 1000000ull + fraction;
  if (value < kMinDialHz || value > kMaxDialHz) return false;
  *hz = static_cast<uint32_t>(value);
  return true;
}

void format_mhz(uint32_t hz, char* out, size_t size) {
  if (out == nullptr || size == 0) return;
  std::snprintf(out, size, "%u.%06u MHz", static_cast<unsigned>(hz / 1000000u), static_cast<unsigned>(hz % 1000000u));
}

bool entry_key(char* buffer, size_t capacity, char key) {
  if (buffer == nullptr || capacity < 2) return false;
  const size_t length = std::strlen(buffer);
  if (key == '<') {
    if (length == 0) return false;
    buffer[length - 1] = '\0';
    return true;
  }
  if (length + 1 >= capacity) return false;
  if (key == '.') {
    if (std::strchr(buffer, '.') != nullptr) return false;
    if (length == 0) {   // ".5" is allowed; store as "0.5" so the display reads naturally
      if (length + 2 >= capacity) return false;
      buffer[0] = '0';
      buffer[1] = '.';
      buffer[2] = '\0';
      return true;
    }
    buffer[length] = '.';
    buffer[length + 1] = '\0';
    return true;
  }
  if (key < '0' || key > '9') return false;
  const char* dot = std::strchr(buffer, '.');
  if (dot != nullptr && std::strlen(dot + 1) >= 6) return false;
  if (length == 1 && buffer[0] == '0' && dot == nullptr) {   // "0" then a digit replaces the zero rather than building "05"
    buffer[0] = key;
    return true;
  }
  buffer[length] = key;
  buffer[length + 1] = '\0';
  return true;
}

bool self_check() {
  uint32_t hz = 0;
  return parse_mhz("7.078", &hz) && hz == 7078000u && apply_steps(7078000u, 1, 1000u) == 7079000u;
}

}  // namespace orcsdr::ft8::tuning
