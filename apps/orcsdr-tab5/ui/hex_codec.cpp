#include "hex_codec.hpp"

#include <cstdlib>
#include <cstring>

namespace orcsdr::hex_codec {

bool decode(const char* value, uint8_t* output, size_t output_size) {
  auto digit = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  if (strlen(value) != output_size * 2) return false;
  for (size_t index = 0; index < output_size; ++index) {
    const int high = digit(value[index * 2]);
    const int low = digit(value[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

bool decode_text(const char* value, char* output, size_t output_size) {
  const size_t length = strlen(value);
  if (length % 2 != 0 || length / 2 >= output_size) return false;
  auto digit = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  for (size_t index = 0; index < length / 2; ++index) {
    const int high = digit(value[index * 2]);
    const int low = digit(value[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<char>((high << 4) | low);
  }
  output[length / 2] = '\0';
  return true;
}

bool parse_u32_exact(const char* value, uint32_t* output) {
  if (value == nullptr || output == nullptr || strlen(value) != 8) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 16);
  if (end == nullptr || *end != '\0') return false;
  *output = static_cast<uint32_t>(parsed);
  return true;
}

void write(const uint8_t* value, size_t size, void (*put)(char digit)) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t index = 0; index < size; ++index) {
    put(kHex[value[index] >> 4]);
    put(kHex[value[index] & 0x0f]);
  }
}

}  // namespace orcsdr::hex_codec
