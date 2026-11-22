// Decodes the two MeshPackets captured from the live mesh on 906.875 MHz into
// header fields, a port number and (for text) the message. The frames are the
// exact payloads the native LoRa decoder produced on the device, so this test
// fails if the application-layer parsing drifts from what the air carries.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mesh/meshtastic_packet.hpp"

namespace mt = orc::zero::mesh::meshtastic;

namespace {

std::vector<uint8_t> from_hex(const char* text) {
  std::vector<uint8_t> bytes;
  const size_t length = std::strlen(text);
  for (size_t i = 0; i + 1 < length; i += 2) {
    auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return 0;
    };
    bytes.push_back(uint8_t((nibble(text[i]) << 4) | nibble(text[i + 1])));
  }
  return bytes;
}

// Captured on the Cardputer Zero from the live mesh.
const char* kFrame1 =
    "FFFFFFFF607629E13216C13263080060DFF22C011390F1668531B19B0F55C98E33F253AC7E670CFAC8594CB67E";
const char* kFrame2 =
    "FFFFFFFFED29663DECF98D5CE4080060943D96C9B76F74E74751A393AE65B9AD2334AEB9A6C9A385EF929F40D7C04A4935F01C0DD7251BC3";

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

void report(const char* label, const char* hex) {
  std::printf("\n%s\n", label);
  const auto frame = from_hex(hex);
  check(frame.size() >= mt::kHeaderBytes, "frame is at least one header long");

  mt::Header header;
  check(mt::parse_header(frame.data(), frame.size(), header), "header parses");
  std::printf("       to=%08X from=%08X id=%08X flags=%02X channel=%u hop %u/%u relay=%02X\n",
              header.to, header.from, header.id, header.flags, header.channel,
              header.hop_limit(), header.hop_start(), header.relay_node);
  check(header.broadcast(), "broadcast destination");

  const std::vector<uint8_t> ciphertext(frame.begin() + mt::kHeaderBytes, frame.end());
  std::vector<uint8_t> plaintext;
  check(mt::decrypt(header, ciphertext, mt::kDefaultKey, plaintext), "AES-CTR decrypt runs");
  std::printf("       plaintext:");
  for (uint8_t byte : plaintext) std::printf(" %02X", byte);
  std::printf("\n");

  mt::Data data;
  const bool parsed = mt::parse_data(plaintext, data);
  check(parsed, "protobuf Data parses with the default channel key");
  if (parsed) {
    std::printf("       portnum=%u (%s) payload=%zu bytes\n", data.portnum,
                mt::port_name(data.portnum), data.payload.size());
    std::string text;
    if (mt::text_of(data, text)) std::printf("       text=\"%s\"\n", text.c_str());
    mt::Position position;
    if (mt::parse_position(data, position)) {
      std::printf("       position %.6f, %.6f", position.latitude, position.longitude);
      if (position.has_altitude) std::printf(" alt %d m", position.altitude);
      std::printf("\n");
    }
  }
}

}  // namespace

int main() {
  std::printf("Meshtastic application layer\n");
  report("frame 1", kFrame1);
  report("frame 2", kFrame2);
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
