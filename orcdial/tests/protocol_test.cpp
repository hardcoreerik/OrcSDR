#include "../src/control/protocol.hpp"
#include "../src/state.hpp"
#include <cassert>

int main() {
  orc::Packet p;
  p.type = orc::Type::tune_relative;
  p.sequence = 123; p.value = -5000; p.frequency_hz = 146520000;
  p.dashboard = uint8_t(orc::Dashboard::weather);
  p.action = 7; p.view = 2; p.revision = 49; p.selected = -2; p.item_count = 17;
  uint8_t wire[orc::packet_size]; orc::encode(p, wire);
  orc::Packet decoded;
  assert(orc::decode(wire, sizeof wire, decoded));
  assert(decoded.sequence == 123 && decoded.value == -5000);
  assert(decoded.frequency_hz == 146520000);
  assert(decoded.dashboard == uint8_t(orc::Dashboard::weather));
  assert(decoded.action == 7 && decoded.view == 2 && decoded.revision == 49);
  assert(decoded.selected == -2 && decoded.item_count == 17);
  assert(orc::carousel_count == 17 && orc::carousel_index(orc::Dashboard::weather) == 3);
  assert(!orc::valid_dashboard(uint8_t(orc::devices_entry)));
  assert(!orc::valid_dashboard(11) && !orc::valid_dashboard(17));
  wire[20] ^= 1;
  assert(!orc::decode(wire, sizeof wire, decoded));
  assert(!orc::decode(wire, sizeof wire - 1, decoded));
  assert(orc::newer_sequence(10, 9));
  assert(!orc::newer_sequence(9, 10));
  assert(!orc::newer_sequence(10, 10));
  assert(orc::newer_sequence(1, 0xffffffffu));
  assert(orc::clamp_frequency(-1) == 24000);
  assert(orc::clamp_frequency(2000000000) == 1766000000);
  p.type = orc::Type::semantic_action; p.value = 0;
  orc::encode(p, wire);
  assert(orc::decode(wire, sizeof wire, decoded) && decoded.type == orc::Type::semantic_action);
  assert(!orc::valid_type(uint8_t(orc::Type::semantic_action) + 1));
}
