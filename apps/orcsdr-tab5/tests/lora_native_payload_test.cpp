#include "lora_native_decoder.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

using orcsdr::lora_native::Config;
using orcsdr::lora_native::Packet;

int main() {
  // Meshtastic Data: TEXT_MESSAGE_APP (port 1), payload "hello".
  const uint8_t plaintext[] = {
      0x44, 0x33, 0x22, 0x11,  // destination
      0xd4, 0xc3, 0xb2, 0xa1,  // sender
      0x40, 0x30, 0x20, 0x10,  // packet id
      0, 0, 0, 0,              // flags/channel/next-hop
      0x08, 0x01, 0x12, 0x05, 'h', 'e', 'l', 'l', 'o',
  };
  Config config{};
  Packet packet{};
  const size_t workspace_before = orcsdr::lora_native::capture_workspace_bytes();
  packet.sender = 99;
  assert(orcsdr::lora_native::decode_payload(plaintext, sizeof(plaintext), config, &packet));
  assert(packet.destination == 0x11223344u);
  assert(packet.sender == 0xa1b2c3d4u);
  assert(packet.packet_id == 0x10203040u);
  assert(packet.port == 1 && !packet.encrypted);
  assert(std::strcmp(packet.text, "hello") == 0);
  assert(orcsdr::lora_native::capture_workspace_bytes() == workspace_before);

  const auto is_cleared = [](const Packet& p) {
    return p.sender == 0 && p.destination == 0 && p.packet_id == 0 &&
           p.latitude_e7 == INT32_MAX && p.longitude_e7 == INT32_MAX &&
           p.port == 0 && !p.encrypted && p.text[0] == '\0';
  };
  uint8_t malformed[17]{};
  malformed[16] = 0x0a;  // truncated length-delimited protobuf field
  uint8_t unknown_ciphertext[17]{};
  unknown_ciphertext[16] = 0x5d;
  packet.sender = 7;
  assert(!orcsdr::lora_native::decode_payload(plaintext, 16, config, &packet));
  assert(is_cleared(packet));
  assert(orcsdr::lora_native::decode_payload(malformed, sizeof(malformed), config, &packet));
  assert(packet.encrypted && std::strcmp(packet.text, "ENCRYPTED FRAME") == 0);
  assert(orcsdr::lora_native::decode_payload(unknown_ciphertext, sizeof(unknown_ciphertext),
                                             config, &packet));
  assert(packet.encrypted && std::strcmp(packet.text, "ENCRYPTED FRAME") == 0);
  assert(!orcsdr::lora_native::decode_payload(plaintext, 256, config, &packet));
  assert(is_cleared(packet));
  assert(!orcsdr::lora_native::decode_payload(nullptr, sizeof(malformed), config, &packet));
  assert(is_cleared(packet));
  assert(!orcsdr::lora_native::decode_payload(plaintext, sizeof(plaintext), config, nullptr));
}
