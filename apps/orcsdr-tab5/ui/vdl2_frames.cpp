// SPDX-License-Identifier: AGPL-3.0-only
#include "vdl2_frames.hpp"
#include <cstring>

namespace orcsdr::vdl2 {
namespace {
// Reflected 0x1021. Source: dumpvdl2/src/crc.c and libacars/libacars/crc.c.
uint16_t crc_update(uint16_t crc, uint8_t b) {
  crc ^= b;
  for (int i = 0; i < 8; ++i) crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1u) ? 0x8408u : 0u));
  return crc;
}
}
uint16_t avlc_crc(const uint8_t* bytes, size_t count, uint16_t initial) {
  uint16_t crc = initial;
  for (size_t i = 0; i < count; ++i) crc = crc_update(crc, bytes[i]);
  return crc;
}
uint16_t acars_crc(const uint8_t* bytes, size_t count) { return avlc_crc(bytes, count, 0u); }

void FrameDecoder::reset() {
  bit_count_ = 0;
  flag_window_ = 0;
  in_frame_ = false;
  bits_.fill(0);
}

void FrameDecoder::push_bit(bool bit) {
  flag_window_ = static_cast<uint8_t>((flag_window_ >> 1) | (bit ? 0x80u : 0u));
  if (in_frame_) {
    if (bit_count_ >= bits_.size() * 8u) {
      ++stats_.overflow;
      in_frame_ = false;
      bit_count_ = 0;
    } else {
      const size_t index = bit_count_ / 8u;
      const uint8_t mask = static_cast<uint8_t>(1u << (bit_count_ & 7u));
      if ((bit_count_ & 7u) == 0u) bits_[index] = 0;
      if (bit) bits_[index] |= mask;
      ++bit_count_;
    }
  }
  // HDLC flags are bit-palindromic; shift-register representation is 0x7e.
  if (flag_window_ == 0x7eu) {
    ++stats_.flags;
    if (in_frame_ && bit_count_ >= 8u) {
      bit_count_ -= 8u; // remove the terminating flag before unstuffing
      if (bit_count_ != 0u) complete();
    }
    in_frame_ = true;
    bit_count_ = 0;
  }
}

void FrameDecoder::complete() {
  // De-stuff into bytes, require octet alignment, and fail closed on bad runs.
  size_t output_bits = 0;
  unsigned ones = 0;
  std::memset(frame_.data(), 0, frame_.size());
  for (size_t i = 0; i < bit_count_; ++i) {
    const bool bit = ((bits_[i / 8u] >> (i & 7u)) & 1u) != 0u;
    if (ones == 5u) {
      if (bit) { ++stats_.malformed; return; }
      ones = 0;
      continue;
    }
    if (output_bits == frame_.size() * 8u) { ++stats_.overflow; return; }
    if (bit) frame_[output_bits / 8u] |= static_cast<uint8_t>(1u << (output_bits & 7u));
    ++output_bits;
    ones = bit ? ones + 1u : 0u;
  }
  if ((output_bits & 7u) != 0u) { ++stats_.malformed; return; }
  accept_frame(frame_.data(), output_bits / 8u);
}

uint32_t FrameDecoder::addr(const uint8_t* p) {
  // Four transmitted octets carry the 28-bit address in reversed bit order.
  uint32_t value = (uint32_t(p[0] >> 1) | (uint32_t(p[1]) << 6) |
                   (uint32_t(p[2]) << 13) | (uint32_t(p[3] & 0xfe) << 20));
  uint32_t reversed = 0;
  for (int i = 0; i < 28; ++i) { reversed = (reversed << 1) | (value & 1u); value >>= 1; }
  return reversed;
}

bool FrameDecoder::extract_acars(const uint8_t* p, size_t n, acars::Message& m) const {
  // ARINC 618: ACARS block starting at mode, no SOH, ending BCS + DEL.
  // Inner CRC has independent integrity from outer AVLC FCS.
  if (n < 16 || n > acars::kMaxBlockBytes || p[n - 1] != 0x7fu ||
      acars_crc(p, n - 1) != 0u || (p[0] & 0x7fu) != '2') return false;
  const uint8_t end = static_cast<uint8_t>(p[n - 4] & 0x7fu);
  if (end != 0x03u && end != 0x17u) return false;
  m = {};
  m.frequency_hz = frequency_hz_;
  m.mode = '2';
  m.raw_bytes = static_cast<uint16_t>(n);
  for (unsigned i = 0; i < 7; ++i) m.tail[i] = static_cast<char>(p[1u + i] & 0x7fu);
  m.acknowledgement = static_cast<char>(p[8] & 0x7fu);
  m.label[0] = static_cast<char>(p[9] & 0x7fu);
  m.label[1] = static_cast<char>(p[10] & 0x7fu);
  if (m.label[1] == 0x7f) m.label[1] = '?'; // existing UI convention
  m.block_id = static_cast<char>(p[11] & 0x7fu);
  m.end_marker = end;
  // Some legitimate uplink acknowledgements contain no STX and text.
  if ((p[12] & 0x7fu) != 0x02u) return n == 16u && !(m.block_id >= '0' && m.block_id <= '9');
  if (n < 17u) return false;
  const size_t text_size = n - 17u; // 12-byte header + STX + text + ETX + 2 BCS + DEL
  if (text_size > sizeof(m.text)) return false;
  m.has_text = true;
  m.text_length = static_cast<uint16_t>(text_size);
  for (size_t i = 0; i < text_size; ++i) m.text[i] = static_cast<char>(p[13u + i] & 0x7fu);
  if (text_size >= 10u && m.block_id >= '0' && m.block_id <= '9')
    for (unsigned i = 0; i < 6; ++i) m.flight[i] = m.text[4u + i];
  return true;
}

bool FrameDecoder::accept_frame(const uint8_t* bytes, size_t count) {
  if (!bytes || count < 11u || count > kMaxAvlcBytes) { ++stats_.malformed; return false; }
  // CRC-16/ISO-HDLC initial 0xffff, reflected poly 0x8408, good FCS residue 0xf0b8.
  if (avlc_crc(bytes, count) != 0xf0b8u) { ++stats_.frames_crc_bad; return false; }
  ++stats_.frames_crc_ok;
  FrameView view{};
  view.raw = bytes;
  view.raw_size = count;
  view.destination = addr(bytes);
  view.source = addr(bytes + 4);
  view.control = bytes[8];
  acars::Message acars_msg{};
  if ((view.control & 1u) == 0u && count >= 12u + 16u + 2u &&
      bytes[9] == 0xffu && bytes[10] == 0xffu && bytes[11] == 0x01u) {
    const size_t inner_size = count - 12u - 2u;
    if (extract_acars(bytes + 12, inner_size, acars_msg)) {
      view.is_acars = true;
      view.acars_message = &acars_msg;
      ++stats_.acars_ok;
    } else ++stats_.acars_bad;
  } else ++stats_.raw_frames;
  if (callback_) callback_(context_, view);
  return true;
}
} // namespace orcsdr::vdl2
