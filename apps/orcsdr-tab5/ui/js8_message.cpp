#include "js8_message.hpp"

#include <cstdio>
#include <cstring>

namespace orcsdr::js8::message {
namespace {

constexpr char kChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /@";   // index 36 = blank, 37 = '/', 38 = '@'
constexpr size_t kCharCount = sizeof(kChars) - 1;

uint32_t bits_to_value(const uint8_t* bits, size_t count) {
  uint32_t v = 0;
  for (size_t i = 0; i < count; ++i) v = (v << 1) | (bits[i] & 1u);
  return v;
}

bool is_alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'); }
bool is_letter(char c) { return c >= 'A' && c <= 'Z'; }

}  // namespace

bool unpack_callsign(uint32_t value, char out[12]) {
  if (value >= (1u << 28)) return false;
  char suffix[3];
  for (int i = 0; i < 3; ++i) {
    suffix[i] = kChars[10 + value % 27];   // 10 + 0..26 = 'A'..'Z' and the blank at index 36
    value /= 27;
  }
  const char digit = kChars[value % 10];
  value /= 10;
  const char second = kChars[value % 36];
  value /= 36;
  if (value >= kCharCount) return false;
  const char first = kChars[value];
  // string = first, second, digit, then the suffix characters in reverse extraction order (the first extracted is the last character)
  char raw[7] = {first, second, digit, suffix[2], suffix[1], suffix[0], '\0'};
  // plausibility: no compound/group markers; prefix characters alphanumeric or blank; suffix letters followed only by blanks
  for (size_t i = 0; i < 6; ++i)
    if (raw[i] == '/' || raw[i] == '@') return false;
  if (!(first == ' ' || is_alnum(first)) || !(second == ' ' || is_alnum(second))) return false;
  if (first == ' ' && second == ' ') return false;
  if (first != ' ' && second == ' ') return false;   // a blank may only lead
  bool seen_blank = false;
  for (size_t i = 3; i < 6; ++i) {
    if (raw[i] == ' ') seen_blank = true;
    else if (seen_blank || !is_letter(raw[i])) return false;
  }
  if (raw[3] == ' ') return false;   // at least one suffix letter
  // trim blanks at both ends
  size_t begin = 0, end = 6;
  while (begin < end && raw[begin] == ' ') ++begin;
  while (end > begin && raw[end - 1] == ' ') --end;
  if (end - begin < 3 || end - begin > 6) return false;
  std::memcpy(out, raw + begin, end - begin);
  out[end - begin] = '\0';
  return true;
}

bool unpack_directed(const uint8_t payload[codec::kTextBits], Directed* out) {
  if (out == nullptr) return false;
  *out = Directed{};
  if (!unpack_callsign(bits_to_value(payload + 3, 28), out->source)) return false;
  if (!unpack_callsign(bits_to_value(payload + 31, 28), out->destination)) return false;
  out->command = static_cast<uint8_t>(bits_to_value(payload + 59, 5));
  out->extra = static_cast<uint8_t>(bits_to_value(payload + 64, 8));
  const int numeric = out->extra & 0x3F;   // 0 = absent; 1..61 map to -30..+30 (offset 31)
  if (numeric >= 1 && numeric <= 61) {
    out->snr_present = true;
    out->snr_db = static_cast<int8_t>(numeric - 31);
  }
  return true;
}

bool render(const codec::Fields& fields, Rendered* out) {
  if (out == nullptr) return false;
  *out = Rendered{};
  if (fields.kind != kKindDirected || fields.flags != kFlagsFirstAndLast) return false;
  Directed d;
  if (!unpack_directed(fields.payload, &d)) return false;
  if (d.command != kCommandHeartbeatSnr || !d.snr_present) return false;   // only this command is verified on real signals
  std::snprintf(out->text, sizeof(out->text), "%s: %s HEARTBEAT SNR %+03d", d.source, d.destination, static_cast<int>(d.snr_db));
  std::memcpy(out->source, d.source, sizeof(out->source));
  out->directed = d;
  out->ok = true;
  return true;
}

bool self_check() {
  char call[12];
  return kCharCount == 39 && unpack_callsign(231616421u, call) && std::strcmp(call, "WO7I") == 0;
}

}  // namespace orcsdr::js8::message
