#include "ft8_codec.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace orcsdr::ft8::codec {
namespace {

constexpr uint32_t kTokenCount = 2063592u;
constexpr uint32_t kHash22Count = 4194304u;
constexpr uint64_t kHashPrime = 47055833459ull;
constexpr char kHashAlphabet[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/";
constexpr char kCallFirst[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr char kCallSecond[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr char kCallDigit[] = "0123456789";
constexpr char kCallSuffix[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr std::array<uint8_t, 7> kCostas{{3, 1, 4, 0, 6, 5, 2}};

int alphabet_index(const char* alphabet, char value) {
  const char* p = std::strchr(alphabet, value);
  return p ? static_cast<int>(p - alphabet) : -1;
}

bool normalize_ascii(const char* input, char* output, std::size_t capacity,
                     std::size_t max_chars) {
  if (!input || !output || capacity < max_chars + 1) return false;
  std::size_t length = std::strlen(input);
  while (length > 0 && input[length - 1] == ' ') --length;
  std::size_t first = 0;
  while (first < length && input[first] == ' ') ++first;
  length -= first;
  if (length == 0 || length > max_chars) return false;

  std::fill(output, output + max_chars, ' ');
  for (std::size_t i = 0; i < length; ++i) {
    const unsigned char raw = static_cast<unsigned char>(input[first + i]);
    if (raw > 0x7f) return false;
    output[i] = static_cast<char>(std::toupper(raw));
  }
  output[max_chars] = '\0';
  return true;
}

uint16_t crc14_bits(const uint8_t* source, std::size_t source_bits) {
  // Literal form of the public protocol definition: the 77 source bits are
  // extended to 82 bits with zeros. Polynomial long division by the degree-14
  // polynomial then operates across 82 + 14 = 96 bit positions. This mirrors
  // the authors' public-domain gen_crc14 reference algorithm without sharing
  // implementation code with a decoder.
  constexpr std::array<uint8_t, 15> polynomial{{
      1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1}};
  std::array<uint8_t, 96> message{};
  for (std::size_t i = 0; i < source_bits && i < 77; ++i)
    message[i] = static_cast<uint8_t>(source[i] & 1u);

  std::array<uint8_t, 15> remainder{};
  std::copy_n(message.begin(), remainder.size(), remainder.begin());
  for (std::size_t i = 0; i < 82; ++i) {
    remainder[14] = message[i + 14];
    if (remainder[0]) {
      for (std::size_t j = 0; j < remainder.size(); ++j)
        remainder[j] ^= polynomial[j];
    }
    for (std::size_t j = 0; j + 1 < remainder.size(); ++j)
      remainder[j] = remainder[j + 1];
    remainder[14] = 0;
  }

  uint16_t crc = 0;
  for (std::size_t i = 0; i < 14; ++i)
    crc = static_cast<uint16_t>((crc << 1u) | remainder[i]);
  return crc;
}

}  // namespace

uint16_t crc14(const PayloadBits& payload) {
  return crc14_bits(payload.data(), payload.size());
}

MessageBits append_crc(const PayloadBits& payload) {
  MessageBits result{};
  std::copy(payload.begin(), payload.end(), result.begin());
  const uint16_t crc = crc14(payload);
  for (std::size_t i = 0; i < kCrcBits; ++i)
    result[kPayloadBits + i] = static_cast<uint8_t>((crc >> (13u - i)) & 1u);
  return result;
}

bool crc_valid(const MessageBits& message) {
  PayloadBits payload{};
  std::copy_n(message.begin(), kPayloadBits, payload.begin());
  const uint16_t expected = crc14(payload);
  uint16_t actual = 0;
  for (std::size_t i = 0; i < kCrcBits; ++i)
    actual = static_cast<uint16_t>((actual << 1u) | (message[kPayloadBits + i] & 1u));
  return actual == expected;
}

bool encode_standard_callsign(const char* callsign, uint32_t* c28) {
  if (!callsign || !c28) return false;

  char raw[7]{};
  std::size_t length = std::strlen(callsign);
  std::size_t begin = 0;
  while (begin < length && callsign[begin] == ' ') ++begin;
  while (length > begin && callsign[length - 1] == ' ') --length;
  const std::size_t count = length - begin;
  if (count < 3 || count > 6) return false;
  for (std::size_t i = 0; i < count; ++i) {
    const unsigned char ch = static_cast<unsigned char>(callsign[begin + i]);
    if (ch > 0x7f) return false;
    raw[i] = static_cast<char>(std::toupper(ch));
  }

  // A standard call is a one- or two-character prefix (at least one prefix
  // character must be a letter), one decimal digit, then one to three
  // letters. The protocol's six mixed-radix positions are [prefix2][digit]
  // [suffix3], with a one-character prefix represented by a blank first
  // position and a short suffix padded with trailing blanks.
  std::size_t digit_pos = count;
  for (std::size_t i = 0; i < count; ++i) {
    if (raw[i] >= '0' && raw[i] <= '9') {
      if (digit_pos != count) return false;  // exactly one digit in a standard call
      digit_pos = i;
    }
  }
  if (digit_pos != 1 && digit_pos != 2) return false;
  const std::size_t suffix_len = count - digit_pos - 1;
  if (suffix_len < 1 || suffix_len > 3) return false;

  bool prefix_has_letter = false;
  for (std::size_t i = 0; i < digit_pos; ++i) {
    const char ch = raw[i];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) return false;
    prefix_has_letter = prefix_has_letter || (ch >= 'A' && ch <= 'Z');
  }
  if (!prefix_has_letter) return false;
  for (std::size_t i = digit_pos + 1; i < count; ++i)
    if (raw[i] < 'A' || raw[i] > 'Z') return false;

  char field[7] = "      ";
  if (digit_pos == 1) {
    field[1] = raw[0];
  } else {
    field[0] = raw[0];
    field[1] = raw[1];
  }
  field[2] = raw[digit_pos];
  for (std::size_t i = 0; i < suffix_len; ++i)
    field[3 + i] = raw[digit_pos + 1 + i];

  const int i1 = alphabet_index(kCallFirst, field[0]);
  const int i2 = alphabet_index(kCallSecond, field[1]);
  const int i3 = alphabet_index(kCallDigit, field[2]);
  const int i4 = alphabet_index(kCallSuffix, field[3]);
  const int i5 = alphabet_index(kCallSuffix, field[4]);
  const int i6 = alphabet_index(kCallSuffix, field[5]);
  if (i1 < 0 || i2 < 0 || i3 < 0 || i4 < 0 || i5 < 0 || i6 < 0) return false;

  uint32_t value = kTokenCount + kHash22Count;
  value += static_cast<uint32_t>(36 * 10 * 27 * 27 * 27 * i1);
  value += static_cast<uint32_t>(10 * 27 * 27 * 27 * i2);
  value += static_cast<uint32_t>(27 * 27 * 27 * i3);
  value += static_cast<uint32_t>(27 * 27 * i4);
  value += static_cast<uint32_t>(27 * i5 + i6);
  *c28 = value;
  return true;
}

bool callsign_hashes(const char* callsign, CallsignHashes* hashes) {
  if (!hashes) return false;
  char field[12]{};
  if (!normalize_ascii(callsign, field, sizeof(field), 11)) return false;

  uint64_t value = 0;
  for (std::size_t i = 0; i < 11; ++i) {
    const int digit = alphabet_index(kHashAlphabet, field[i]);
    if (digit < 0) return false;
    value = value * 38u + static_cast<uint64_t>(digit);
  }
  const uint64_t mixed = value * kHashPrime;  // unsigned wrap is protocol-defined modulo 2^64.
  hashes->h10 = static_cast<uint16_t>(mixed >> 54u);
  hashes->h12 = static_cast<uint16_t>(mixed >> 52u);
  hashes->h22 = static_cast<uint32_t>(mixed >> 42u);
  return true;
}

void restore_ft4_payload(PayloadBits* payload) {
  if (!payload) return;
  // Franke/Somerville/Taylor, QEX July/August 2020, Appendix A.
  // The paper prints this protocol-defined 77-bit pseudo-random sequence
  // explicitly and states that the receiver applies XOR a second time.
  static constexpr char kFt4Xor[] =
      "0100101001011110100010"
      "0110110100101100001000"
      "1010011110010101010110"
      "11111000101";
  static_assert(sizeof(kFt4Xor) - 1 == kPayloadBits);
  for (std::size_t i = 0; i < payload->size(); ++i)
    (*payload)[i] ^= static_cast<uint8_t>(kFt4Xor[i] - '0');
}

uint8_t gray_tone(uint8_t b0, uint8_t b1, uint8_t b2) {
  static constexpr std::array<uint8_t, 8> kMap{{0, 1, 3, 2, 5, 6, 4, 7}};
  const uint8_t bits = static_cast<uint8_t>(((b0 & 1u) << 2u) |
                                             ((b1 & 1u) << 1u) |
                                             (b2 & 1u));
  return kMap[bits];
}

bool codeword_to_data_tones(const CodewordBits& codeword, DataTones* tones) {
  if (!tones) return false;
  for (std::size_t symbol = 0; symbol < kDataSymbols; ++symbol) {
    const std::size_t bit = symbol * 3;
    (*tones)[symbol] = gray_tone(codeword[bit], codeword[bit + 1], codeword[bit + 2]);
  }
  return true;
}

ChannelTones frame_data_tones(const DataTones& data) {
  ChannelTones result{};
  std::copy(kCostas.begin(), kCostas.end(), result.begin());
  std::copy_n(data.begin(), 29, result.begin() + 7);
  std::copy(kCostas.begin(), kCostas.end(), result.begin() + 36);
  std::copy_n(data.begin() + 29, 29, result.begin() + 43);
  std::copy(kCostas.begin(), kCostas.end(), result.begin() + 72);
  return result;
}

bool self_check() {
  constexpr char kVector[] =
      "00000000000000000000000000100000010011011111110011011100100010100001010000001";
  PayloadBits payload{};
  for (std::size_t i = 0; i < payload.size(); ++i)
    payload[i] = static_cast<uint8_t>(kVector[i] - '0');
  if (crc14(payload) != 5497u || !crc_valid(append_crc(payload))) return false;

  uint32_t c28 = 0;
  CallsignHashes hashes{};
  PayloadBits ft4_probe{};
  ft4_probe[0] = 1;
  const auto original = ft4_probe;
  restore_ft4_payload(&ft4_probe);
  restore_ft4_payload(&ft4_probe);

  return encode_standard_callsign("K1ABC", &c28) && c28 == 10214965u &&
         callsign_hashes("PJ4/K1ABC", &hashes) && hashes.h10 == 346u &&
         hashes.h12 == 1387u && hashes.h22 == 1420834u &&
         gray_tone(0, 1, 1) == 2u && gray_tone(1, 0, 0) == 5u &&
         ft4_probe == original;
}

}  // namespace orcsdr::ft8::codec
