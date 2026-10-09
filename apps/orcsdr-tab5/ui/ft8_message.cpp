#include "ft8_message.hpp"

#include <array>
#include <cstdio>
#include <cstring>

namespace orcsdr::ft8::message {
namespace {

constexpr uint32_t kTokenCount = 2063592u;
constexpr uint32_t kHash22Count = 4194304u;
constexpr uint32_t kStandardBase = kTokenCount + kHash22Count;
constexpr uint32_t kStandardCount =
    37u * 36u * 10u * 27u * 27u * 27u;
constexpr uint16_t kGridCount = 18u * 18u * 10u * 10u;  // 32400

constexpr char kCallFirst[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr char kCallSecond[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr char kCallDigit[] = "0123456789";
constexpr char kCallSuffix[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";

uint32_t read_bits(const codec::PayloadBits& payload, std::size_t offset,
                   std::size_t count) {
  uint32_t value = 0;
  for (std::size_t i = 0; i < count; ++i)
    value = (value << 1u) | (payload[offset + i] & 1u);
  return value;
}

void trim_field(const char* field, std::size_t length, char* output,
                std::size_t capacity) {
  std::size_t begin = 0;
  while (begin < length && field[begin] == ' ') ++begin;
  std::size_t end = length;
  while (end > begin && field[end - 1] == ' ') --end;
  const std::size_t count =
      capacity == 0 ? 0 : (end - begin < capacity - 1 ? end - begin
                                                       : capacity - 1);
  if (count != 0) std::memcpy(output, field + begin, count);
  if (capacity != 0) output[count] = '\0';
}

bool render_callsign(const Callsign& call, bool suffix, uint8_t type,
                     char* output, std::size_t capacity) {
  if (output == nullptr || capacity == 0) return false;
  if (call.kind == CallsignKind::hash22 ||
      call.kind == CallsignKind::unsupported)
    return false;
  const char* suffix_text = suffix ? (type == 1 ? "/R" : "/P") : "";
  const int written =
      std::snprintf(output, capacity, "%s%s", call.text, suffix_text);
  return written >= 0 && static_cast<std::size_t>(written) < capacity;
}

bool render_standard(StandardMessage* message) {
  char first[20]{};
  char second[20]{};
  if (!render_callsign(message->first, message->first_suffix, message->type,
                       first, sizeof(first)) ||
      !render_callsign(message->second, message->second_suffix, message->type,
                       second, sizeof(second)))
    return false;

  if (message->extra.kind == ExtraKind::blank) {
    const int n = std::snprintf(message->text, sizeof(message->text),
                                "%s %s", first, second);
    return n >= 0 && static_cast<std::size_t>(n) < sizeof(message->text);
  }

  char extra[12]{};
  if (message->roger && message->extra.kind == ExtraKind::report) {
    const int n = std::snprintf(extra, sizeof(extra), "R%s",
                                message->extra.text);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(extra)) return false;
  } else if (message->roger) {
    const int n = std::snprintf(extra, sizeof(extra), "R %s",
                                message->extra.text);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(extra)) return false;
  } else {
    const int n =
        std::snprintf(extra, sizeof(extra), "%s", message->extra.text);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(extra)) return false;
  }

  const int n = std::snprintf(message->text, sizeof(message->text),
                              "%s %s %s", first, second, extra);
  return n >= 0 && static_cast<std::size_t>(n) < sizeof(message->text);
}

}  // namespace

bool decode_c28(uint32_t value, Callsign* out) {
  if (out == nullptr || value >= (1u << 28u)) return false;
  *out = Callsign{};

  if (value == 0) {
    out->kind = CallsignKind::de;
    std::strcpy(out->text, "DE");
    return true;
  }
  if (value == 1) {
    out->kind = CallsignKind::qrz;
    std::strcpy(out->text, "QRZ");
    return true;
  }
  if (value == 2) {
    out->kind = CallsignKind::cq;
    std::strcpy(out->text, "CQ");
    return true;
  }

  // Other values below kTokenCount are directed/modified CQ tokens. They are
  // protocol-defined, but this first clean-room parser does not guess their
  // text representation.
  if (value < kTokenCount) return false;

  if (value < kStandardBase) {
    out->kind = CallsignKind::hash22;
    out->hash22 = value - kTokenCount;
    return true;
  }

  uint32_t mixed = value - kStandardBase;
  if (mixed >= kStandardCount) return false;

  const uint8_t i6 = static_cast<uint8_t>(mixed % 27u);
  mixed /= 27u;
  const uint8_t i5 = static_cast<uint8_t>(mixed % 27u);
  mixed /= 27u;
  const uint8_t i4 = static_cast<uint8_t>(mixed % 27u);
  mixed /= 27u;
  const uint8_t i3 = static_cast<uint8_t>(mixed % 10u);
  mixed /= 10u;
  const uint8_t i2 = static_cast<uint8_t>(mixed % 36u);
  mixed /= 36u;
  const uint8_t i1 = static_cast<uint8_t>(mixed);
  if (i1 >= 37u) return false;

  char field[7]{};
  field[0] = kCallFirst[i1];
  field[1] = kCallSecond[i2];
  field[2] = kCallDigit[i3];
  field[3] = kCallSuffix[i4];
  field[4] = kCallSuffix[i5];
  field[5] = kCallSuffix[i6];
  field[6] = '\0';
  trim_field(field, 6, out->text, sizeof(out->text));

  // Canonicality check: only accept values our independent standard-call
  // encoder maps back to exactly the same c28.
  uint32_t roundtrip = 0;
  if (!codec::encode_standard_callsign(out->text, &roundtrip) ||
      roundtrip != value) {
    *out = Callsign{};
    return false;
  }

  out->kind = CallsignKind::standard;
  return true;
}

bool decode_g15(uint16_t value, Extra* out) {
  if (out == nullptr || value >= (1u << 15u)) return false;
  *out = Extra{};

  if (value < kGridCount) {
    uint16_t v = value;
    const uint8_t d4 = static_cast<uint8_t>(v % 10u);
    v /= 10u;
    const uint8_t d3 = static_cast<uint8_t>(v % 10u);
    v /= 10u;
    const uint8_t i2 = static_cast<uint8_t>(v % 18u);
    v /= 18u;
    const uint8_t i1 = static_cast<uint8_t>(v);
    if (i1 >= 18u) return false;
    out->kind = ExtraKind::grid;
    out->text[0] = static_cast<char>('A' + i1);
    out->text[1] = static_cast<char>('A' + i2);
    out->text[2] = static_cast<char>('0' + d3);
    out->text[3] = static_cast<char>('0' + d4);
    out->text[4] = '\0';
    return true;
  }

  if (value == kGridCount + 1u) {
    out->kind = ExtraKind::blank;
    out->text[0] = '\0';
    return true;
  }
  if (value == kGridCount + 2u) {
    out->kind = ExtraKind::rrr;
    std::strcpy(out->text, "RRR");
    return true;
  }
  if (value == kGridCount + 3u) {
    out->kind = ExtraKind::rr73;
    std::strcpy(out->text, "RR73");
    return true;
  }
  if (value == kGridCount + 4u) {
    out->kind = ExtraKind::seventy_three;
    std::strcpy(out->text, "73");
    return true;
  }

  // QEX grid4_to_g15 mapping: report -30 maps to 32405 and +99 maps
  // to 32534, equivalently g15 = 32400 + 35 + report.
  if (value >= kGridCount + 5u && value <= kGridCount + 134u) {
    const int report = static_cast<int>(value) -
                       static_cast<int>(kGridCount) - 35;
    if (report < -30 || report > 99) return false;
    out->kind = ExtraKind::report;
    out->report_db = static_cast<int16_t>(report);
    const int n =
        std::snprintf(out->text, sizeof(out->text), "%+03d", report);
    return n >= 0 && static_cast<std::size_t>(n) < sizeof(out->text);
  }

  // 32400 and the remainder of the 15-bit space are not accepted by this
  // standard-message parser.
  return false;
}

bool unpack_standard(const codec::PayloadBits& payload, StandardMessage* out) {
  if (out == nullptr) return false;
  *out = StandardMessage{};

  const uint8_t type = static_cast<uint8_t>(read_bits(payload, 74, 3));
  if (type != 1u && type != 2u) return false;

  const uint32_t first_c28 = read_bits(payload, 0, 28);
  const bool first_suffix = read_bits(payload, 28, 1) != 0;
  const uint32_t second_c28 = read_bits(payload, 29, 28);
  const bool second_suffix = read_bits(payload, 57, 1) != 0;
  const bool roger = read_bits(payload, 58, 1) != 0;
  const uint16_t g15 = static_cast<uint16_t>(read_bits(payload, 59, 15));

  StandardMessage message{};
  message.type = type;
  message.first_suffix = first_suffix;
  message.second_suffix = second_suffix;
  message.roger = roger;

  if (!decode_c28(first_c28, &message.first) ||
      !decode_c28(second_c28, &message.second) ||
      !decode_g15(g15, &message.extra))
    return false;

  const bool first_renderable =
      message.first.kind != CallsignKind::hash22 &&
      message.first.kind != CallsignKind::unsupported;
  const bool second_renderable =
      message.second.kind != CallsignKind::hash22 &&
      message.second.kind != CallsignKind::unsupported;
  message.fully_renderable = first_renderable && second_renderable;

  if (message.fully_renderable && !render_standard(&message)) return false;
  *out = message;
  return true;
}

bool self_check() {
  Callsign call{};
  uint32_t c28 = 0;
  if (!codec::encode_standard_callsign("K1ABC", &c28) ||
      !decode_c28(c28, &call) ||
      call.kind != CallsignKind::standard ||
      std::strcmp(call.text, "K1ABC") != 0)
    return false;

  Extra extra{};
  return decode_g15(5u * 1800u + 13u * 100u + 4u * 10u + 2u, &extra) &&
         extra.kind == ExtraKind::grid &&
         std::strcmp(extra.text, "FN42") == 0 &&
         decode_g15(kGridCount + 28u, &extra) &&
         extra.kind == ExtraKind::report &&
         extra.report_db == -7;
}

}  // namespace orcsdr::ft8::message
