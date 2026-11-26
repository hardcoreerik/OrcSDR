// Receive-only bounded NXDN48/NXDN96 channel IQ frontend.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "orcsdr_nxdn/decoder.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace orcsdr::nxdn {
struct IQSample { float re = 0; float im = 0; };
using DibitSink = void (*)(std::uint8_t, void*) noexcept;
// 48 kHz channelized complex IQ; no radio, USB, tuner or resampler ownership.
// Reset both this object and the decoder on every retune/discontinuity.
template<std::size_t MaxSps>
class BasicFrontend final {
    static_assert(MaxSps == 10 || MaxSps == 20, "supported NXDN storage geometries");
public:
    // input_rate_hz: 48000 for both modes (validated default). NXDN48 may be fed a 24000 Hz stream (half the work; the
    // symbol geometry then equals NXDN96 at 48 kHz). The caller supplies the decimated stream; nothing resamples here.
    explicit BasicFrontend(Mode mode, double input_rate_hz = 48000.0) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool feed(const IQSample* iq, std::size_t count,
                            DibitSink sink, void* user = nullptr) noexcept;
    [[nodiscard]] double carrier_offset_hz() const noexcept { return carrier_hz_; }
    [[nodiscard]] std::uint64_t symbols_seen() const noexcept { return symbols_; }
private:
    void build_taps() noexcept;
    Mode mode_;
    double fs_ = 48000.0;
    std::size_t sps_ = 10;
    std::size_t channel_n_ = 0;
    std::size_t rrc_n_ = 0;
    std::array<float, 18 * MaxSps + 1> channel_taps_{};
    std::array<float, 16 * MaxSps + 1> rrc_taps_{};
    std::array<IQSample, 18 * MaxSps + 1> channel_hist_{};
    std::array<float, 16 * MaxSps + 1> rrc_hist_{};
    std::size_t channel_pos_ = 0, rrc_pos_ = 0;
    IQSample prev_iq_{1,0};
    bool have_iq_ = false;
    float prev_rrc_ = 0;
    bool have_rrc_ = false;
    double carrier_hz_ = 0, nco_phase_ = 0;
    double disc_sum_ = 0, disc_sumsq_ = 0;
    unsigned disc_count_ = 0;
    double mm_mu_ = 0;
    float prev_sym_ = 0;
    bool have_sym_ = false;
    double symbol_dc_ = 0, symbol_abs_ = 0;
    std::uint64_t symbols_ = 0;
};
// Preserve the original general-rate API. Production's 24k/2400 and
// 48k/4800 inputs both use 10 samples/symbol; no unused 20-sps histories.
using Frontend = BasicFrontend<20>;
using CompactFrontend = BasicFrontend<10>;
} // namespace orcsdr::nxdn
