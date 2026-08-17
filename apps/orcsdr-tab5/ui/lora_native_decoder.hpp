#pragma once

#include <cstddef>
#include <climits>
#include <cstdint>

namespace orcsdr::lora_native {

constexpr size_t kMaxPacketsPerCapture = 4;
constexpr size_t kPacketTextBytes = 112;

struct Config {
  const uint8_t* authorized_psk = nullptr;
  size_t authorized_psk_bytes = 0;
};

struct Packet {
  uint32_t sender = 0;
  uint32_t destination = 0;
  uint32_t packet_id = 0;
  int32_t latitude_e7 = INT32_MAX;
  int32_t longitude_e7 = INT32_MAX;
  int16_t cfo_tenths_hz = 0;
  uint16_t port = 0;
  bool encrypted = false;
  char text[kPacketTextBytes]{};
};

struct Stats {
  uint32_t captures = 0;
  uint32_t preambles = 0;
  uint32_t header_failures = 0;
  uint32_t crc_ok = 0;
  uint32_t crc_failures = 0;
  uint32_t encrypted = 0;
  uint32_t decode_millis = 0;
  int16_t raw_cfo_tenths_hz = 0;
  int16_t cfo_tenths_hz = 0;
  bool ready = false;
};

// Allocates fixed decoder scratch space once. Call before starting RTL streaming.
bool initialize();

// Decodes an immutable CU8 capture. This function is intentionally task-only:
// it may take milliseconds and must never run from the RTL IQ callback.
size_t decode_capture(const uint8_t* cu8, size_t bytes, uint32_t sample_rate_sps,
                      uint8_t spreading_factor, uint32_t bandwidth_hz, uint32_t frequency_hz,
                      const Config& config, Packet* packets, size_t packet_capacity,
                      Stats* stats);

bool self_check();

}  // namespace orcsdr::lora_native
