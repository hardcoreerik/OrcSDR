// OrcSDR DMR D1 48 kS/s IQ -> 4,800 dibits/s: offline host-only front end.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dmr::d1 {

inline constexpr std::uint32_t kSampleRate = 48000;
inline constexpr std::uint32_t kSymbolRate = 4800;
inline constexpr std::uint32_t kSamplesPerSymbol = kSampleRate / kSymbolRate;
inline constexpr float kOuterDeviationHz = 1944.0F;
inline constexpr float kInnerDeviationHz = 648.0F;

struct ComplexSample { float re; float im; }; // interleaved CF32 IQ, caller-owned
struct Symbol {
    std::uint8_t dibit = 0; // 0,1,2,3 in ETSI bit1/bit0 order
    float error_hz = 0.0F;  // signed slicer residual; NOT bit error probability
    std::uint64_t sample_index = 0; // approximate center, relative to last reset
};

struct ProcessResult {
    std::size_t consumed = 0;
    std::size_t produced = 0;
};

struct Stats {
    std::uint64_t samples = 0;
    std::uint64_t symbols = 0;
    std::uint64_t locks = 0;
    std::uint64_t lost_locks = 0;
    std::uint64_t discontinuities = 0;
    std::uint64_t invalid_iq = 0;
    bool locked = false;
    std::uint8_t sync_kind = 255; // D0 SyncKind index or unknown
    float sync_correlation = 0.0F; // 0..1; correlation is NOT CRC evidence
    float carrier_offset_hz = 0.0F;
    float deviation_per_level_hz = 0.0F;
    float rms_iq = 0.0F; // relative to input scaling; not calibrated dBm
    float decision_rms_hz = 0.0F; // smoothed error to nearest of 4 levels
    float timing_step_samples = 10.0F;
};

// No dynamic allocation or hidden output queue. If output fills, consume only
// samples through the last emitted symbol and return counts for caller replay.
// A sync-qualified acquisition begins emitting at the NEXT symbol; the 24
// acquisition sync dibits themselves are not backfilled.
class Frontend final {
public:
    Frontend() noexcept;
    void reset() noexcept;                 // full reset, clears counters
    void discontinuity() noexcept;         // resets DSP/lock, retains counters
    ProcessResult process(const ComplexSample* samples, std::size_t count,
                          Symbol* output, std::size_t output_capacity) noexcept;
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    [[nodiscard]] static constexpr std::size_t static_storage_bytes() noexcept {
        return sizeof(Frontend);
    }

private:
    static constexpr std::size_t kPulseTaps = 61;
    static constexpr std::size_t kHistory = 512;
    void reset_tracking() noexcept;
    void consume_one(const ComplexSample& sample) noexcept;
    void try_acquire() noexcept;
    [[nodiscard]] float filtered_at(std::uint64_t i) const noexcept;
    [[nodiscard]] float interpolate(double i) const noexcept;
    bool decide(Symbol& out) noexcept;

    std::array<float, kPulseTaps> taps_{};
    std::array<float, kPulseTaps> discr_history_{};
    std::array<float, kHistory> filtered_history_{};
    std::size_t discr_head_ = 0;
    std::uint64_t processed_ = 0;
    ComplexSample previous_{};
    bool have_previous_ = false;
    bool locked_ = false;
    double next_center_ = 0.0;
    double timing_step_ = 10.0;
    float cfo_hz_ = 0.0F;
    float level_hz_ = kInnerDeviationHz;
    float prev_level_ = 0.0F;
    bool have_prev_level_ = false;
    float decision_mse_ = 0.0F;
    std::uint32_t bad_symbols_ = 0;
    float iq_power_ = 0.0F;
    Stats stats_{};
};

// ETSI TS 102 361-1 Table 10.3: 00 -> +1; 01 -> +3;
// 10 -> -1; 11 -> -3. D0's 48-bit sync uses MSB-first dibits.
[[nodiscard]] constexpr int dibit_level(std::uint8_t dibit) noexcept {
    return dibit == 0U ? 1 : dibit == 1U ? 3 : dibit == 2U ? -1 : dibit == 3U ? -3 : 0;
}
[[nodiscard]] constexpr std::uint8_t level_dibit(float scaled) noexcept {
    return scaled >= 2.0F ? 1U : scaled >= 0.0F ? 0U : scaled >= -2.0F ? 2U : 3U;
}

} // namespace orcsdr::dmr::d1