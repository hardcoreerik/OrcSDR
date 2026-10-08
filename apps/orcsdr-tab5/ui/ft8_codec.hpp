#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::ft8::codec {

constexpr std::size_t kPayloadBits = 77;
constexpr std::size_t kCrcBits = 14;
constexpr std::size_t kMessageBits = 91;
constexpr std::size_t kCodewordBits = 174;
constexpr std::size_t kDataSymbols = 58;
constexpr std::size_t kSyncSymbols = 21;
constexpr std::size_t kChannelSymbols = 79;

using PayloadBits = std::array<uint8_t, kPayloadBits>;
using MessageBits = std::array<uint8_t, kMessageBits>;
using CodewordBits = std::array<uint8_t, kCodewordBits>;
using DataTones = std::array<uint8_t, kDataSymbols>;
using ChannelTones = std::array<uint8_t, kChannelSymbols>;

struct CallsignHashes {
  uint16_t h10 = 0;
  uint16_t h12 = 0;
  uint32_t h22 = 0;
};

// FT8 CRC-14. The input is the 77 source bits in transmission order.
// Returns the 14-bit integer represented by the CRC bits, MSB first.
uint16_t crc14(const PayloadBits& payload);

// Appends the CRC bits MSB first to form the 91-bit message word.
MessageBits append_crc(const PayloadBits& payload);

// Verifies the CRC carried in bits 77..90 of a 91-bit message word.
bool crc_valid(const MessageBits& message);

// Encodes a standard amateur callsign into the protocol c28 space.
// This function deliberately handles standard calls only; CQ/DE/QRZ and
// hashed/non-standard calls are separate protocol cases.
bool encode_standard_callsign(const char* callsign, uint32_t* c28);

// Calculates the FT8 10-, 12-, and 22-bit callsign hashes. Input is
// normalized to uppercase ASCII and padded to 11 characters with spaces.
bool callsign_hashes(const char* callsign, CallsignHashes* hashes);

// Maps a three-bit FT8 codeword group to an 8-FSK tone using QEX Table 3.
uint8_t gray_tone(uint8_t b0, uint8_t b1, uint8_t b2);

// Converts the 174 codeword bits into the 58 data-tone values.
bool codeword_to_data_tones(const CodewordBits& codeword, DataTones* tones);

// Inserts the three 7-symbol FT8 Costas synchronization blocks around the
// two 29-symbol data halves, yielding the 79 transmitted tone indices.
ChannelTones frame_data_tones(const DataTones& data);

bool self_check();

}  // namespace orcsdr::ft8::codec
