// OrcSDR independently implemented receive-only NXDN metadata foundation.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::nxdn {

inline constexpr std::size_t kFrameDibits = 192;
inline constexpr std::uint32_t kFsw20 = 0xCDF59U;

enum class Mode : std::uint8_t { nxdn48, nxdn96 };
enum class Evidence : std::uint8_t { sacch_crc_valid };

// FSW/LICH-only frame observation is NOT trusted radio identity evidence.
struct FrameObservation {
    std::uint64_t frame_start_dibit = 0;
    Mode mode = Mode::nxdn48;
    std::uint8_t fsw_symbol_errors = 0;
    std::uint8_t rf_channel = 0;
    std::uint8_t functional_channel = 0;
    std::uint8_t steal_option = 0;
    std::uint8_t direction = 0;
    std::uint8_t voice_mask = 0; // four raw, UNVALIDATED vocoder codeword positions
    std::array<std::array<std::uint8_t,36>,4> voice_dibits{};
};
using FrameSink = void (*)(const FrameObservation&, void*) noexcept;

// There are no inferred source/destination, talkgroup, site or voice fields.
// An event means LICH passed strict parity/geometry AND SACCH passed FEC/CRC.
struct Event {
    std::uint64_t frame_start_dibit = 0;
    Mode mode = Mode::nxdn48;
    Evidence evidence = Evidence::sacch_crc_valid;
    std::uint8_t rf_channel = 0;
    std::uint8_t functional_channel = 0;
    std::uint8_t steal_option = 0;
    std::uint8_t direction = 0;
    std::uint8_t structure = 0;
    std::uint8_t ran = 0;
    std::array<std::uint8_t, 18> sacch_fragment_bits{};
    std::uint8_t sacch_viterbi_distance = 0;
};

struct Counters {
    std::uint64_t dibits_seen = 0;
    std::uint64_t exact_sync_windows = 0;
    std::uint64_t near_sync_windows = 0;
    std::uint64_t voice_codeword_candidates = 0;
    std::uint64_t lich_valid = 0;
    std::uint64_t sacch_valid = 0;
    std::uint64_t sacch_rejected = 0;
};

using EventSink = void (*)(const Event&, void*) noexcept;

// Consumes hard-sliced on-air dibits (0..3, MSB-first), not IQ, PCM, or packed bits.
// Mode is supplied by the channelizer; it cannot be guessed from dibits alone.
// Stateless with respect to any SDR/USB/tuner; fixed memory, no allocation.
class Decoder final {
public:
    explicit Decoder(Mode mode, std::uint8_t max_sync_symbol_errors = 0) noexcept
        : mode_(mode), max_sync_errors_(max_sync_symbol_errors) {}
    void reset() noexcept;
    // Invalid input is rejected atomically, including invalid enum mode.
    [[nodiscard]] bool feed(const std::uint8_t* dibits, std::size_t length,
                            EventSink callback, void* user = nullptr,
                            FrameSink frame_callback = nullptr, void* frame_user = nullptr) noexcept;
    [[nodiscard]] Counters counters() const noexcept { return counters_; }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
private:
    void inspect_window(EventSink callback, void* user,
                        FrameSink frame_callback, void* frame_user) noexcept;
    std::uint8_t at(std::uint64_t absolute_dibit) const noexcept {
        return history_[static_cast<std::size_t>(absolute_dibit % kFrameDibits)];
    }
    Mode mode_;
    std::uint8_t max_sync_errors_ = 0;
    std::array<std::uint8_t, kFrameDibits> history_{};
    Counters counters_{};
};

} // namespace orcsdr::nxdn
