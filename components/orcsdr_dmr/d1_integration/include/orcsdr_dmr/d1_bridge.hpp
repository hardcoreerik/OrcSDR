#pragma once
// OrcSDR D1-C: bounded streaming glue between the existing IQ 4FSK frontend
// and hard-dibit Tier II protocol decoder. No radio, I/O, UI or allocation.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "frontend.hpp"
#include "orcsdr_dmr/d1.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::dmr::d1::integration {

struct Event {
    d1::Event decoded{};         // Respect the decoder's evidence gates.
    bool sample_time_known = false;
    std::uint64_t burst_start_sample = 0; // Relative to most recent discontinuity/reset.
    std::uint32_t stream_epoch = 0;       // Separate from any UTC or station identity.
};
using EventSink = void (*)(const Event&, void*) noexcept;

struct Stats {
    std::uint64_t iq_samples = 0;
    std::uint64_t dibits_sent = 0;
    std::uint64_t protocol_events = 0;
    std::uint64_t validated_identities = 0;
    std::uint64_t acquisitions = 0;
    std::uint64_t acquisition_bursts_missed = 0;
    std::uint64_t lost_locks = 0;
    std::uint64_t invalid_calls = 0;
    std::uint32_t stream_epoch = 0;
};
struct Result {
    std::size_t consumed = 0;
    std::size_t events = 0;
    bool ok = true; // False only for a programmer/API error, not noisy RF.
};

// Host-portable, callback-only receiver. Caller retains IQ memory and event
// data; neither module queues caller-owned pointers. Callers serialize access.
//
// IMPORTANT: The existing frontend starts after its *acquisition* SYNC. It
// cannot recover the 54 pre-sync payload dibits in that burst. This bridge
// does not invent those missing symbols. The protocol decoder may decode
// subsequent *complete* bursts after continuous lock, but initial short
// transmissions can be missed. An eventual pre-roll extension needs a
// separate measured acceptance gate.
class Receiver final {
public:
    Receiver() noexcept = default;
    void reset() noexcept;
    void discontinuity() noexcept;
    Result process(const ComplexSample* input, std::size_t count,
                   EventSink callback = nullptr, void* user = nullptr) noexcept;
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    [[nodiscard]] const d1::Stats& frontend_stats() const noexcept { return frontend_.stats(); }
    [[nodiscard]] std::uint64_t protocol_dibits() const noexcept {
        return protocol_.dibits_consumed();
    }
    [[nodiscard]] static constexpr std::size_t static_storage_bytes() noexcept {
        return sizeof(Receiver);
    }
private:
    static constexpr std::size_t kStepSamples = 8;
    static constexpr std::size_t kSampleHistory = 256;
    static void on_protocol(const d1::Event& event, void* context) noexcept;
    void deliver(const d1::Event& event) noexcept;
    void clear_protocol() noexcept;

    d1::Frontend frontend_{};
    d1::Decoder protocol_{};
    std::array<std::uint64_t, kSampleHistory> sample_history_{};
    std::uint64_t prior_locks_ = 0;
    std::uint64_t prior_losses_ = 0;
    Stats stats_{};
    EventSink active_callback_ = nullptr;
    void* active_user_ = nullptr;
    std::size_t call_events_ = 0;
};

}  // namespace orcsdr::dmr::d1::integration
