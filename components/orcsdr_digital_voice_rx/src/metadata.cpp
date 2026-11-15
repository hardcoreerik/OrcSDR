// OrcSDR independent, receive-only YSF FICH and D-STAR DV radio headers.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_digital_voice_rx/metadata.hpp"
#include <array>
#include <cstdint>
#include <limits>

namespace orcsdr::digital_voice_rx {
namespace {
constexpr std::uint16_t kInfinity = 30000U;
inline unsigned bit_count(std::uint32_t n) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<unsigned>(__builtin_popcount(n));
#else
    unsigned count = 0;
    while (n) { n &= n - 1U; ++count; }
    return count;
#endif
}
inline std::uint8_t parity(std::uint32_t n) noexcept {
    return static_cast<std::uint8_t>(bit_count(n) & 1U);
}
// Four-state and 16-state terminated rate-1/2 Viterbi. Only Hamming hard
// decisions are supported at this stage. No heap and bounded path memory.
template <unsigned Memory, std::size_t Steps>
bool viterbi(const std::array<std::uint8_t, Steps * 2>& wire,
             std::array<std::uint8_t, Steps>& decoded,
             std::uint16_t& distance, unsigned generator0,
             unsigned generator1) noexcept {
    constexpr unsigned States = 1U << Memory;
    constexpr unsigned Mask = States - 1U;
    std::array<std::uint16_t, States> previous{};
    std::array<std::uint16_t, States> next{};
    std::array<std::array<std::uint8_t, States>, Steps> traceback{};
    previous.fill(kInfinity);
    previous[0] = 0;
    for (std::size_t i = 0; i < Steps; ++i) {
        if (wire[2 * i] > 1 || wire[2 * i + 1] > 1) return false;
        next.fill(kInfinity);
        for (unsigned state = 0; state < States; ++state) {
            if (previous[state] == kInfinity) continue;
            for (unsigned bit = 0; bit <= 1; ++bit) {
                const auto next_state = ((state << 1U) | bit) & Mask;
                const auto reg = (state << 1U) | bit;
                const auto encoded0 = parity(reg & generator0);
                const auto encoded1 = parity(reg & generator1);
                const unsigned metric = (encoded0 != wire[2 * i]) +
                                        (encoded1 != wire[2 * i + 1]);
                const auto candidate = static_cast<std::uint16_t>(previous[state] + metric);
                if (candidate < next[next_state]) {
                    next[next_state] = candidate;
                    traceback[i][next_state] = static_cast<std::uint8_t>(state);
                }
            }
        }
        previous = next;
    }
    // Transmitter appends Memory zero tail bits. Forbid arbitrary survivor
    // final states rather than calling unchecked bytes a valid header.
    if (previous[0] == kInfinity) return false;
    distance = previous[0];
    unsigned state = 0;
    for (std::size_t i = Steps; i-- > 0;) {
        decoded[i] = static_cast<std::uint8_t>(state & 1U);
        state = traceback[i][state];
    }
    if (state != 0) return false;
    for (std::size_t i = Steps - Memory; i < Steps; ++i)
        if (decoded[i] != 0) return false;
    return true;
}

// Yaesu Am. Radio Digital Specs v1.02 Appendix A, G=[I12|P]. Also independently
// published by IRIG 106 Appendix Q. These are algebraic constants, not source
// copied from a third-party GPL implementation.
constexpr std::array<std::uint16_t, 12> kGolayParity = {
    0xC75U,0x63BU,0xF68U,0x7B4U,0x3DAU,0xD99U,
    0x6CDU,0x367U,0xDC6U,0xA97U,0x93EU,0x8EBU};
std::uint32_t golay_codeword(std::uint16_t value) noexcept {
    std::uint16_t remainder = 0;
    for (unsigned i = 0; i < 12; ++i)
        if (value & (1U << (11U - i))) remainder ^= kGolayParity[i];
    return (static_cast<std::uint32_t>(value) << 12U) | remainder;
}
bool golay_decode(std::uint32_t received, std::uint16_t& word,
                  std::uint8_t& corrected) noexcept {
    unsigned best = 25;
    bool unique = false;
    std::uint16_t candidate_word = 0;
    for (unsigned candidate = 0; candidate < 4096; ++candidate) {
        const auto dist = bit_count(received ^ golay_codeword(static_cast<std::uint16_t>(candidate)));
        if (dist < best) {
            best = dist;
            candidate_word = static_cast<std::uint16_t>(candidate);
            unique = true;
        } else if (dist == best) {
            unique = false;
        }
    }
    if (!unique || best > 3) return false;
    word = candidate_word;
    corrected = static_cast<std::uint8_t>(best);
    return true;
}
std::uint16_t ysf_crc(const std::array<std::uint8_t, 4>& data) noexcept {
    std::uint16_t crc = 0;
    for (auto byte : data) {
        crc ^= static_cast<std::uint16_t>(byte) << 8U;
        for (int j = 0; j < 8; ++j)
            crc = static_cast<std::uint16_t>((crc << 1U) ^ ((crc & 0x8000U) ? 0x1021U : 0U));
    }
    return static_cast<std::uint16_t>(~crc);
}
std::uint16_t dstar_crc(const std::array<std::uint8_t, 41>& data) noexcept {
    std::uint16_t crc = 0xFFFFU;
    for (unsigned i = 0; i < 39; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = static_cast<std::uint16_t>((crc >> 1U) ^ ((crc & 1U) ? 0x8408U : 0U));
    }
    return static_cast<std::uint16_t>(~crc);
}

// JARL voice DV header uses an x^7+x^4+1 all-ones PRBS restarted at
// each radio header; the encoded bits are scrambled individually on air.
inline std::uint8_t dstar_prbs(std::uint8_t& state) noexcept {
    const auto out = static_cast<std::uint8_t>((state >> 6U) & 1U);
    const auto feedback = static_cast<std::uint8_t>(((state >> 6U) ^ (state >> 3U)) & 1U);
    state = static_cast<std::uint8_t>(((state << 1U) | feedback) & 0x7FU);
    return out;
}

template <std::size_t N>
bool extract_callsign(const std::array<std::uint8_t, 41>& raw, unsigned start,
                      std::array<char, N>& dest) noexcept {
    for (std::size_t i = 0; i < N - 1; ++i) {
        const auto ch = raw[start + i];
        // JARL callsigns are padded uppercase alphanumerics, spaces and
        // portable/repeater separators. Restrict the repertoire rather than
        // injecting an arbitrary CRC-valid RF string into downstream JSON.
        if (!(ch == ' ' || ch == '/' || ch == '-' ||
              (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')))
            return false;
        dest[i] = static_cast<char>(ch);
    }
    dest[N - 1] = '\0';
    return true;
}
} // namespace

namespace ysf {
bool decode_fich(const std::uint8_t* bits, Fich& output) noexcept {
    if (!bits) return false;
    // 5x20 dibit interleave: wire dibit i is original dibit
    // (i%20)*5 + i/20. This is the inverse of Yaesu Fig 4-26.
    std::array<std::uint8_t, 200> wire{};
    for (unsigned i = 0; i < 100; ++i) {
        const auto source = ((i % 20U) * 5U + i / 20U) * 2U;
        if (bits[2 * i] > 1 || bits[2 * i + 1] > 1) return false;
        wire[source] = bits[2 * i];
        wire[source + 1U] = bits[2 * i + 1U];
    }
    std::array<std::uint8_t, 100> decoded{};
    std::uint16_t viterbi_distance = 0;
    // ysf polynomials (23,35) octal: current bit in LSB, past four bits
    // shifted higher. G1 taps x^4,x^3,x^0 and G2 x^4,x^2,x^1,x^0.
    if (!viterbi<4,100>(wire, decoded, viterbi_distance, 0b11001U, 0b10111U))
        return false;
    std::array<std::uint8_t, 6> raw{};
    unsigned corrected_total = 0;
    for (unsigned block = 0; block < 4; ++block) {
        std::uint32_t code = 0;
        for (unsigned bit = 0; bit < 24; ++bit)
            code = (code << 1U) | decoded[block * 24U + bit];
        std::uint16_t value = 0;
        std::uint8_t corrected = 0;
        if (!golay_decode(code, value, corrected)) return false;
        corrected_total += corrected;
        for (unsigned bit = 0; bit < 12; ++bit) {
            const auto index = block * 12U + bit;
            raw[index / 8U] |= static_cast<std::uint8_t>(((value >> (11U - bit)) & 1U) << (7U - index % 8U));
        }
    }
    std::array<std::uint8_t,4> information{raw[0],raw[1],raw[2],raw[3]};
    const std::uint16_t checked_crc = ysf_crc(information);
    const std::uint16_t received_crc = static_cast<std::uint16_t>((raw[4] << 8U) | raw[5]);
    if (received_crc != checked_crc) return false;
    Fich candidate{};
    candidate.raw = (static_cast<std::uint32_t>(raw[0]) << 24U) |
                    (static_cast<std::uint32_t>(raw[1]) << 16U) |
                    (static_cast<std::uint32_t>(raw[2]) << 8U) | raw[3];
    candidate.crc = received_crc;
    candidate.viterbi_distance = viterbi_distance;
    candidate.golay_bit_corrections = static_cast<std::uint8_t>(corrected_total);
    const auto v = candidate.raw;
    candidate.frame_type = static_cast<std::uint8_t>((v >> 30U) & 3U);
    candidate.callsign_type = static_cast<std::uint8_t>((v >> 28U) & 3U);
    candidate.call_mode = static_cast<std::uint8_t>((v >> 26U) & 3U);
    candidate.block_number = static_cast<std::uint8_t>((v >> 24U) & 3U);
    candidate.block_total = static_cast<std::uint8_t>((v >> 22U) & 3U);
    candidate.frame_number = static_cast<std::uint8_t>((v >> 19U) & 7U);
    candidate.frame_total = static_cast<std::uint8_t>((v >> 16U) & 7U);
    candidate.dev = (v & (1U << 14U)) != 0U;
    candidate.message_route = static_cast<std::uint8_t>((v >> 11U) & 7U);
    candidate.voip = (v & (1U << 10U)) != 0U;
    candidate.data_type = static_cast<std::uint8_t>((v >> 8U) & 3U);
    candidate.sql = (v & (1U << 7U)) != 0U;
    candidate.squelch_code = static_cast<std::uint8_t>(v & 0x7FU);
    output = candidate;
    return true;
}
void Scanner::reset() noexcept { *this = Scanner{}; }
bool Scanner::push(std::uint8_t bit, Fich& event, std::uint64_t& frame_start_bit) noexcept {
    if (bit > 1) { sync_shift_ = 0; collecting_ = false; collected_ = 0; payload_skip_ = 0; ++bits_seen_; return false; }
    ++bits_seen_;
    if (payload_skip_ != 0U) { --payload_skip_; return false; }
    if (collecting_) {
        fich_bits_[collected_++] = bit;
        if (collected_ != kFichBits) return false;
        collecting_ = false;
        collected_ = 0;
        sync_shift_ = 0;
        payload_skip_ = static_cast<std::uint16_t>(kFrameBits - 40 - kFichBits);
        if (!decode_fich(fich_bits_.data(), event)) return false;
        frame_start_bit = frame_start_;
        return true;
    }
    sync_shift_ = ((sync_shift_ << 1U) | bit) & ((1ULL << 40U) - 1U);
    if (sync_shift_ == kFrameSync40) {
        frame_start_ = bits_seen_ - 40;
        collecting_ = true;
        collected_ = 0;
    }
    return false;
}
} // namespace ysf

namespace dstar {
bool decode_header(const std::uint8_t* bits, Header& output) noexcept {
    if (!bits) return false;
    std::array<std::uint8_t, 660> after_scramble{};
    std::uint8_t lfsr = 0x7FU;
    for (unsigned i = 0; i < 660; ++i) {
        if (bits[i] > 1) return false;
        after_scramble[i] = static_cast<std::uint8_t>(bits[i] ^ dstar_prbs(lfsr));
    }
    // JARL Appendix Ap2 (24-row, 28-column; last twelve rows are short).
    // Each transmittable matrix cell indexes the convolutional bit to pick.
    std::array<std::uint8_t, 660> trellis{};
    unsigned transmitted = 0;
    for (unsigned row = 0; row < 24; ++row)
        for (unsigned col = 0; col < 28; ++col) {
            const unsigned original = row + 24U * col;
            if (original < 660U) trellis[original] = after_scramble[transmitted++];
        }
    if (transmitted != 660U) return false;
    std::array<std::uint8_t, 330> decoded{};
    std::uint16_t distance = 0;
    if (!viterbi<2,330>(trellis, decoded, distance, 0b111U, 0b101U)) return false;
    std::array<std::uint8_t,41> raw{};
    // JARL Ap2.1 specifies LSB-first bit input within each header byte.
    for (unsigned i = 0; i < 328; ++i)
        raw[i / 8U] |= static_cast<std::uint8_t>(decoded[i] << (i % 8U));
    const auto crc = dstar_crc(raw);
    if (raw[39] != static_cast<std::uint8_t>(crc & 255U) ||
        raw[40] != static_cast<std::uint8_t>(crc >> 8U)) return false;
    Header candidate{};
    for (unsigned i = 0; i < 3; ++i) candidate.flags[i] = raw[i];
    if (!extract_callsign(raw,3,candidate.rpt2) ||
        !extract_callsign(raw,11,candidate.rpt1) ||
        !extract_callsign(raw,19,candidate.ur) ||
        !extract_callsign(raw,27,candidate.my) ||
        !extract_callsign(raw,35,candidate.suffix)) return false;
    candidate.crc = crc;
    candidate.viterbi_distance = distance;
    candidate.data = (raw[0] & 0x80U) != 0;
    candidate.via_repeater = (raw[0] & 0x40U) != 0;
    output = candidate;
    return true;
}
void Scanner::reset() noexcept { *this = Scanner{}; }
bool Scanner::push(std::uint8_t bit, Header& event, std::uint64_t& header_start_bit) noexcept {
    if (bit > 1U) { sync_shift_ = 0; collecting_ = false; collected_ = 0; ++bits_seen_; return false; }
    ++bits_seen_;
    if (collecting_) {
        header_bits_[collected_++] = bit;
        if (collected_ != kEncodedHeaderBits) return false;
        collecting_ = false;
        collected_ = 0;
        sync_shift_ = 0;
        if (!decode_header(header_bits_.data(), event)) return false;
        header_start_bit = header_start_;
        return true;
    }
    sync_shift_ = static_cast<std::uint16_t>(((sync_shift_ << 1U) | bit) & 0x7FFFU);
    if (sync_shift_ == kFrameSync15) {
        header_start_ = bits_seen_ - 15U;
        collecting_ = true;
        collected_ = 0;
    }
    return false;
}
} // namespace dstar
} // namespace orcsdr::digital_voice_rx
