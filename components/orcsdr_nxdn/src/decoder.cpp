// NXDN TS 1-A sections 4.4–4.6: frame, LICH, SACCH. Independent implementation.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_nxdn/decoder.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace orcsdr::nxdn {
namespace {
constexpr std::size_t kSacchCodeBits = 60;
constexpr std::size_t kSacchUnpuncturedBits = 72;
constexpr std::size_t kSacchDecodedBits = 36;
constexpr std::uint16_t kInfinity = 10000;

std::uint8_t parity(std::uint8_t x) noexcept {
    x ^= static_cast<std::uint8_t>(x >> 4U);
    x ^= static_cast<std::uint8_t>(x >> 2U);
    x ^= static_cast<std::uint8_t>(x >> 1U);
    return static_cast<std::uint8_t>(x & 1U);
}

std::uint8_t crc6(const std::array<std::uint8_t, kSacchDecodedBits>& bits) noexcept {
    // TS 1-A CRC6: initial register all ones, x^6+x^5+x^2+x+1.
    std::uint8_t reg = 0x3f;
    for (std::size_t i = 0; i < 26; ++i) {
        const bool feedback = (((reg >> 5U) & 1U) ^ bits[i]) != 0;
        reg = static_cast<std::uint8_t>((reg << 1U) & 0x3fU);
        if (feedback) reg = static_cast<std::uint8_t>(reg ^ 0x27U);
    }
    return reg;
}

// Requires one complete 60-bit SACCH block, in transmitted interleaved order.
// Viterbi uses explicit erasures for the twelve punctured positions.
bool decode_sacch(const std::array<std::uint8_t, kSacchCodeBits>& wire,
                  std::array<std::uint8_t, kSacchDecodedBits>& decoded,
                  std::uint8_t& distance) noexcept {
    std::array<std::uint8_t, kSacchCodeBits> ordered{};
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        // 12-row x 5-column block interleaver, inverse 0,12,24...1,13...
        ordered[(i % 5U) * 12U + (i / 5U)] = wire[i];
    }
    std::array<std::int8_t, kSacchUnpuncturedBits> code{};
    code.fill(-1);
    for (std::size_t i = 0, src = 0; i < code.size(); ++i) {
        if (i % 6U != 5U) code[i] = static_cast<std::int8_t>(ordered[src++]);
    }
    std::array<std::uint16_t, 16> previous{}, current{};
    previous.fill(kInfinity);
    previous[0] = 0;
    std::array<std::array<std::uint8_t, 16>, kSacchDecodedBits> survivor{};
    for (std::size_t t = 0; t < kSacchDecodedBits; ++t) {
        current.fill(kInfinity);
        for (std::uint8_t state = 0; state < 16; ++state) {
            if (previous[state] == kInfinity) continue;
            for (std::uint8_t bit = 0; bit < 2; ++bit) {
                const std::uint8_t reg = static_cast<std::uint8_t>((state << 1U) | bit);
                const std::uint8_t next = static_cast<std::uint8_t>(reg & 0x0fU);
                const std::uint8_t g1 = parity(static_cast<std::uint8_t>(reg & 0x19U));
                const std::uint8_t g2 = parity(static_cast<std::uint8_t>(reg & 0x17U));
                std::uint16_t cost = previous[state];
                if (code[2U*t] >= 0 && code[2U*t] != g1) ++cost;
                if (code[2U*t+1U] >= 0 && code[2U*t+1U] != g2) ++cost;
                if (cost < current[next]) {
                    current[next] = cost;
                    survivor[t][next] = state;
                }
            }
        }
        previous = current;
    }
    if (previous[0] == kInfinity) return false;
    distance = static_cast<std::uint8_t>(previous[0]);
    std::uint8_t state = 0; // Four zero tail bits terminate the convolutional code.
    for (std::size_t t = kSacchDecodedBits; t-- > 0;) {
        decoded[t] = static_cast<std::uint8_t>(state & 1U);
        state = survivor[t][state];
    }
    if (state != 0) return false;
    for (std::size_t i = 32; i < decoded.size(); ++i) {
        if (decoded[i] != 0) return false;
    }
    std::uint8_t received_crc = 0;
    for (std::size_t i = 26; i < 32; ++i) {
        received_crc = static_cast<std::uint8_t>((received_crc << 1U) | decoded[i]);
    }
    return received_crc == crc6(decoded);
}
} // namespace

void Decoder::reset() noexcept {
    history_.fill(0);
    counters_ = {};
}

bool Decoder::feed(const std::uint8_t* dibits, std::size_t length,
                   EventSink callback, void* user, FrameSink frame_callback, void* frame_user) noexcept {
    if (mode_ != Mode::nxdn48 && mode_ != Mode::nxdn96) return false;
    if (max_sync_errors_ > 1) return false;
    if (length && !dibits) return false;
    if (length > std::numeric_limits<std::uint64_t>::max() - counters_.dibits_seen)
        return false;
    for (std::size_t i = 0; i < length; ++i) if (dibits[i] > 3U) return false;
    for (std::size_t i = 0; i < length; ++i) {
        history_[static_cast<std::size_t>(counters_.dibits_seen % kFrameDibits)] = dibits[i];
        ++counters_.dibits_seen;
        if (counters_.dibits_seen >= kFrameDibits) inspect_window(callback, user, frame_callback, frame_user);
    }
    return true;
}

void Decoder::inspect_window(EventSink callback, void* user,
                             FrameSink frame_callback, void* frame_user) noexcept {
    const std::uint64_t start = counters_.dibits_seen - kFrameDibits;
    // One dibit FSW error is permitted only with an explicit policy.
    // A candidate is never promoted to identity without a channel CRC.
    std::uint8_t fsw_errors = 0;
    for (std::size_t i = 0; i < 10; ++i) {
        const std::uint8_t target = static_cast<std::uint8_t>((kFsw20 >> (18-2*i)) & 3U);
        if (at(start+i) != target) ++fsw_errors;
    }
    if (fsw_errors > max_sync_errors_) return;
    if (fsw_errors) ++counters_.near_sync_windows;
    else ++counters_.exact_sync_windows;

    // Scrambler: x^9+x^5+1, seed 0xE4; one PN bit per post-FSW dibit MSB.
    std::uint16_t lfsr = 0x0e4U;
    std::array<std::uint8_t, 38> post{}; // first 8 LICH + 30 SACCH dibits
    for (std::size_t i = 0; i < post.size(); ++i) {
        const bool scramble = (lfsr & 1U) != 0;
        const std::uint16_t feedback = static_cast<std::uint16_t>(((lfsr >> 4U) ^ lfsr) & 1U);
        lfsr = static_cast<std::uint16_t>((lfsr >> 1U) | (feedback << 8U));
        post[i] = static_cast<std::uint8_t>(at(start + 10U + i) ^ (scramble ? 2U : 0U));
    }
    std::uint8_t lich = 0;
    std::uint8_t off_bit_errors = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        // LICH low bits are filler ones; one damaged filler is tolerated.
        if ((post[i] & 1U) == 0) ++off_bit_errors;
        lich = static_cast<std::uint8_t>((lich << 1U) | (post[i] >> 1U));
    }
    if (off_bit_errors > 1) return;
    const std::uint8_t rf_channel = static_cast<std::uint8_t>((lich >> 6U) & 3U);
    const std::uint8_t functional = static_cast<std::uint8_t>((lich >> 4U) & 3U);
    const std::uint8_t option = static_cast<std::uint8_t>((lich >> 2U) & 3U);
    const std::uint8_t direction = static_cast<std::uint8_t>((lich >> 1U) & 1U);
    // Parity is over RFCT and FCT only, not the other three LICH data bits.
    if (parity(static_cast<std::uint8_t>(lich >> 4U)) != (lich & 1U)) return;
    ++counters_.lich_valid;
    FrameObservation observation{};
    observation.frame_start_dibit = start;
    observation.mode = mode_;
    observation.fsw_symbol_errors = fsw_errors;
    observation.rf_channel = rf_channel;
    observation.functional_channel = functional;
    observation.steal_option = option;
    observation.direction = direction;
    // RCCH and unsupported functional types NEVER receive voice framing.
    if (rf_channel != 0 && (functional == 0 || functional == 2)) {
        if (option == 3) observation.voice_mask = 0x0f;
        else if (option == 2) observation.voice_mask = 0x03;
        else if (option == 1) observation.voice_mask = 0x0c;
    }
    // PN9 runs across the entire post-FSW frame, including four 36-dibit
    // raw voice positions. These are NOT Golay-decoded vocoder frames.
    for (std::size_t i = 38; i < 182; ++i) {
        const bool scramble = (lfsr & 1U) != 0;
        const std::uint16_t feedback = static_cast<std::uint16_t>(((lfsr >> 4U) ^ lfsr) & 1U);
        lfsr = static_cast<std::uint16_t>((lfsr >> 1U) | (feedback << 8U));
        const std::size_t v = (i-38) / 36;
        const std::size_t n = (i-38) % 36;
        if (observation.voice_mask & (1U << v)) {
            observation.voice_dibits[v][n] = static_cast<std::uint8_t>(
                at(start+10+i) ^ (scramble ? 2U : 0U));
        }
    }
    for (unsigned i=0;i<4;i++)
        if (observation.voice_mask & (1U<<i)) ++counters_.voice_codeword_candidates;
    if (frame_callback) frame_callback(observation, frame_user);
    if (rf_channel == 0 || !(functional == 0U || functional == 2U)) return;

    std::array<std::uint8_t, kSacchCodeBits> wire{};
    for (std::size_t i = 0; i < 30; ++i) {
        wire[2*i] = static_cast<std::uint8_t>(post[8+i] >> 1U);
        wire[2*i+1] = static_cast<std::uint8_t>(post[8+i] & 1U);
    }
    std::array<std::uint8_t, kSacchDecodedBits> decoded{};
    std::uint8_t distance = 0;
    if (!decode_sacch(wire, decoded, distance)) {
        ++counters_.sacch_rejected;
        return;
    }
    ++counters_.sacch_valid;
    Event event{};
    event.frame_start_dibit = start;
    event.mode = mode_;
    event.rf_channel = rf_channel;
    event.functional_channel = functional;
    event.steal_option = option;
    event.direction = direction;
    event.structure = static_cast<std::uint8_t>((decoded[0] << 1U) | decoded[1]);
    for (std::size_t i = 2; i < 8; ++i)
        event.ran = static_cast<std::uint8_t>((event.ran << 1U) | decoded[i]);
    for (std::size_t i = 0; i < event.sacch_fragment_bits.size(); ++i)
        event.sacch_fragment_bits[i] = decoded[8+i];
    event.sacch_viterbi_distance = distance;
    if (callback) callback(event, user);
}
} // namespace orcsdr::nxdn
