#include "ft8_message.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

using orcsdr::ft8::codec::PayloadBits;
using orcsdr::ft8::message::CallsignKind;
using orcsdr::ft8::message::ExtraKind;
using orcsdr::ft8::message::StandardMessage;

void put_bits(PayloadBits* payload, std::size_t offset, std::size_t count,
              uint32_t value) {
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t shift = count - 1 - i;
    (*payload)[offset + i] = static_cast<uint8_t>((value >> shift) & 1u);
  }
}

uint16_t grid(const char* text) {
  assert(std::strlen(text) == 4);
  return static_cast<uint16_t>(
      (text[0] - 'A') * 18 * 100 +
      (text[1] - 'A') * 100 +
      (text[2] - '0') * 10 +
      (text[3] - '0'));
}

PayloadBits standard_payload(const char* first, bool first_suffix,
                             const char* second, bool second_suffix,
                             bool roger, uint16_t g15, uint8_t type) {
  uint32_t c1 = 0, c2 = 0;
  if (std::strcmp(first, "CQ") == 0)
    c1 = 2;
  else
    assert(orcsdr::ft8::codec::encode_standard_callsign(first, &c1));
  if (std::strcmp(second, "CQ") == 0)
    c2 = 2;
  else
    assert(orcsdr::ft8::codec::encode_standard_callsign(second, &c2));

  PayloadBits payload{};
  put_bits(&payload, 0, 28, c1);
  put_bits(&payload, 28, 1, first_suffix ? 1u : 0u);
  put_bits(&payload, 29, 28, c2);
  put_bits(&payload, 57, 1, second_suffix ? 1u : 0u);
  put_bits(&payload, 58, 1, roger ? 1u : 0u);
  put_bits(&payload, 59, 15, g15);
  put_bits(&payload, 74, 3, type);
  return payload;
}

void test_callsign_roundtrip() {
  uint32_t c28 = 0;
  assert(orcsdr::ft8::codec::encode_standard_callsign("W9XYZ", &c28));
  orcsdr::ft8::message::Callsign call{};
  assert(orcsdr::ft8::message::decode_c28(c28, &call));
  assert(call.kind == CallsignKind::standard);
  assert(std::strcmp(call.text, "W9XYZ") == 0);

  assert(orcsdr::ft8::message::decode_c28(0, &call));
  assert(call.kind == CallsignKind::de);
  assert(std::strcmp(call.text, "DE") == 0);
  assert(orcsdr::ft8::message::decode_c28(1, &call));
  assert(call.kind == CallsignKind::qrz);
  assert(orcsdr::ft8::message::decode_c28(2, &call));
  assert(call.kind == CallsignKind::cq);

  // Start of 22-bit hash range is structurally recognized but not rendered.
  assert(orcsdr::ft8::message::decode_c28(2063592u, &call));
  assert(call.kind == CallsignKind::hash22);
  assert(call.hash22 == 0);
}

void test_grid_and_report() {
  orcsdr::ft8::message::Extra extra{};
  assert(orcsdr::ft8::message::decode_g15(grid("FN42"), &extra));
  assert(extra.kind == ExtraKind::grid);
  assert(std::strcmp(extra.text, "FN42") == 0);

  assert(orcsdr::ft8::message::decode_g15(32405, &extra));
  assert(extra.kind == ExtraKind::report && extra.report_db == -30);
  assert(std::strcmp(extra.text, "-30") == 0);
  assert(orcsdr::ft8::message::decode_g15(32428, &extra));
  assert(extra.report_db == -7);
  assert(std::strcmp(extra.text, "-07") == 0);
  assert(orcsdr::ft8::message::decode_g15(32440, &extra));
  assert(extra.report_db == 5);
  assert(std::strcmp(extra.text, "+05") == 0);
  assert(orcsdr::ft8::message::decode_g15(32534, &extra));
  assert(extra.report_db == 99);

  assert(!orcsdr::ft8::message::decode_g15(32400, &extra));
  assert(!orcsdr::ft8::message::decode_g15(32535, &extra));
}

void test_standard_type1_render() {
  StandardMessage message{};
  const auto payload =
      standard_payload("CQ", false, "K1ABC", false, false,
                       grid("FN42"), 1);
  assert(orcsdr::ft8::message::unpack_standard(payload, &message));
  assert(message.fully_renderable);
  assert(std::strcmp(message.text, "CQ K1ABC FN42") == 0);

  const auto report =
      standard_payload("K1ABC", false, "W9XYZ", false, true,
                       32428, 1);
  assert(orcsdr::ft8::message::unpack_standard(report, &message));
  assert(std::strcmp(message.text, "K1ABC W9XYZ R-07") == 0);
}

void test_suffix_families() {
  StandardMessage message{};
  auto payload =
      standard_payload("K1ABC", true, "W9XYZ", true, false,
                       32403, 1);
  assert(orcsdr::ft8::message::unpack_standard(payload, &message));
  assert(std::strcmp(message.text, "K1ABC/R W9XYZ/R RR73") == 0);

  payload = standard_payload("K1ABC", true, "W9XYZ", false, false,
                             32404, 2);
  assert(orcsdr::ft8::message::unpack_standard(payload, &message));
  assert(std::strcmp(message.text, "K1ABC/P W9XYZ 73") == 0);
}

void test_unsupported_type_rejected() {
  StandardMessage message{};
  PayloadBits payload{};
  put_bits(&payload, 74, 3, 4);
  assert(!orcsdr::ft8::message::unpack_standard(payload, &message));
}

}  // namespace

int main() {
  assert(orcsdr::ft8::message::self_check());
  test_callsign_roundtrip();
  test_grid_and_report();
  test_standard_type1_render();
  test_suffix_families();
  test_unsupported_type_rejected();
  return 0;
}
