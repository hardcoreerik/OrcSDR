#include "../src/control/protocol.hpp"
#include "../src/state.hpp"
#include <cassert>

int main() {
  orc::Packet p;
  p.type = orc::Type::tune_relative;
  p.sequence = 123; p.value = -5000; p.frequency_hz = 146520000;
  p.dashboard = uint8_t(orc::Dashboard::weather);
  uint8_t wire[orc::packet_size]; orc::encode(p, wire);
  orc::Packet decoded;
  assert(orc::decode(wire, sizeof wire, decoded));
  assert(decoded.sequence == 123 && decoded.value == -5000);
  assert(decoded.frequency_hz == 146520000);
  assert(decoded.dashboard == uint8_t(orc::Dashboard::weather));
  assert(orc::carousel_count == 16 && orc::carousel_index(orc::Dashboard::weather) == 3);
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
}
