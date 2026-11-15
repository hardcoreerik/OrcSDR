// OrcSDR first-party, receive-only amateur digital voice metadata cores.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::digital_voice_rx {

// Input to these protocol cores is *already-demodulated* hard bits, 0/1,
// in the published on-air transmission order. They do not own a radio,
// antenna, channelizer, CFO correction, vocoder, filesystem or USB callback.
// An actual on-air or ESP32-P4 implementation is a separate, gated work item.
inline constexpr std::uint32_t kChannelRateHz = 48000;
inline constexpr std::uint32_t kSymbolRate = 4800;

namespace ysf {
inline constexpr std::uint64_t kFrameSync40 = 0xD471C9634DULL;
inline constexpr std::size_t kFichBits = 200;
inline constexpr std::size_t kFrameBits = 960;

struct Fich {
    std::uint32_t raw = 0;     // CRC-protected 32-bit FICH, not guessed identities.
    std::uint16_t crc = 0;     // independently checked over the 32 FICH bits.
    std::uint16_t viterbi_distance = 0;
    std::uint8_t golay_bit_corrections = 0;
    std::uint8_t frame_type = 0; // 0 HC, 1 CC, 2 TC, 3 test.
    std::uint8_t callsign_type = 0;
    std::uint8_t call_mode = 0;
    std::uint8_t block_number = 0;
    std::uint8_t block_total = 0;
    std::uint8_t frame_number = 0;
    std::uint8_t frame_total = 0;
    bool dev = false;
    std::uint8_t message_route = 0;
    bool voip = false;
    std::uint8_t data_type = 0; // 2 bits, mode value; not decoded speech.
    bool sql = false;
    std::uint8_t squelch_code = 0;
};

// Input must be exactly 200 bits (as individual bytes 0 or 1) following
// the separately verified 40-bit YSF sync. Rejects FEC, trellis-tail, CRC
// or invalid-bit failures rather than promoting a mere sync to an identity.
[[nodiscard]] bool decode_fich(const std::uint8_t* hard_bits_200, Fich& out) noexcept;

class Scanner final {
public:
    void reset() noexcept;
    // Returns true only on a CRC-validated FICH. First bit is index zero.
    [[nodiscard]] bool push(std::uint8_t bit, Fich& event,
                            std::uint64_t& frame_start_bit) noexcept;
    [[nodiscard]] std::uint64_t bits_seen() const noexcept { return bits_seen_; }
private:
    std::uint64_t sync_shift_ = 0;
    std::uint64_t bits_seen_ = 0;
    std::uint64_t frame_start_ = 0;
    std::uint16_t collected_ = 0;
    std::uint16_t payload_skip_ = 0;
    std::array<std::uint8_t, kFichBits> fich_bits_{};
    bool collecting_ = false;
};
} // namespace ysf

namespace dstar {
inline constexpr std::uint16_t kFrameSync15 = 0b111011001010000U; // JARL 15-bit DV FS.
inline constexpr std::size_t kEncodedHeaderBits = 660;

struct Header {
    std::array<std::uint8_t, 3> flags{};
    std::array<char, 9> rpt2{};
    std::array<char, 9> rpt1{};
    std::array<char, 9> ur{};
    std::array<char, 9> my{};
    std::array<char, 5> suffix{};
    std::uint16_t crc = 0;
    std::uint16_t viterbi_distance = 0;
    bool data = false;
    bool via_repeater = false;
};

// JARL DV header: 41 bytes including X.25 FCS, convolution (7,5) K=3,
// 660 channel bits, JARL 24-position interleave and x^7+x^4+1 scrambler.
// This does NOT decode D-STAR DD mode or speech.
[[nodiscard]] bool decode_header(const std::uint8_t* hard_bits_660,
                                 Header& out) noexcept;
class Scanner final {
public:
    void reset() noexcept;
    [[nodiscard]] bool push(std::uint8_t bit, Header& event,
                            std::uint64_t& header_start_bit) noexcept;
    [[nodiscard]] std::uint64_t bits_seen() const noexcept { return bits_seen_; }
private:
    std::uint16_t sync_shift_ = 0;
    std::uint64_t bits_seen_ = 0;
    std::uint64_t header_start_ = 0;
    std::uint16_t collected_ = 0;
    std::array<std::uint8_t, kEncodedHeaderBits> header_bits_{};
    bool collecting_ = false;
};
} // namespace dstar

} // namespace orcsdr::digital_voice_rx
