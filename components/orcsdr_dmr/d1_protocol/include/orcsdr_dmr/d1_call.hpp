#pragma once
// OrcSDR D1 call manager: turns raw BS voice superframes (d1::VoiceGroup) plus the data bursts around them into
// QUALIFIED voice groups. Fixed memory, no I/O, host-portable. Receive only: nothing here decrypts, strips or
// bypasses privacy. A group is released only when the call is positively identified AND not marked protected.
//
// Privacy states (per slot): UNKNOWN (voice seen, identity or privacy cannot be established), CLEAR (identity
// validated by RS-checked voice LC header, or checksummed embedded LC, or an unbroken same-slot continuation of
// such a call, and no protection indicator seen), PROTECTED (EMB PI bit, PI header, LC protect flag or the
// service-options privacy bit). Only CLEAR groups reach the vocoder; UNKNOWN is never treated as clear.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d1.hpp"
#include <array>
#include <cstdint>

namespace orcsdr::dmr::d1 {

enum class Privacy : std::uint8_t { unknown = 0, clear = 1, protected_voice = 2 };

struct QualifiedGroup {
    std::uint8_t slot = 0;             // 1 or 2
    std::uint8_t colour_code = 0;
    std::uint32_t source_id = 0, destination_id = 0;
    bool group_call = false;
    std::uint64_t epoch = 0;           // start dibit of the call (or its header); stable for the whole call
    bool new_call = false;             // first group of this epoch
    std::uint32_t lost_superframes = 0; // whole 360 ms superframes missing since the previous group of this call
    Privacy privacy = Privacy::clear;  // always clear here
    const VoiceGroup* group = nullptr; // valid during the callback only
};
enum class NoticeKind : std::uint8_t { call_ended, protected_voice, unqualified_voice };
struct Notice {
    NoticeKind kind = NoticeKind::call_ended;
    std::uint8_t slot = 0;             // 0 = could not be attributed to a slot
    std::uint64_t at_dibit = 0;
};
using GroupSink = void (*)(const QualifiedGroup&, void*) noexcept;
using NoticeSink = void (*)(const Notice&, void*) noexcept;

struct CallStats {
    std::uint64_t groups_seen = 0, groups_qualified = 0, groups_protected = 0, groups_unqualified = 0;
    std::uint64_t calls_started = 0, calls_ended = 0, lost_superframes = 0;
    std::uint64_t unknown_slot = 0, conflicting_identity = 0, terminators = 0;
};

class CallManager final {
public:
    void reset() noexcept;
    void set_sinks(GroupSink groups, NoticeSink notices, void* user) noexcept { gs_ = groups; ns_ = notices; user_ = user; }
    // Every BS data-sync event from the decoder (valid or not): a data burst on a slot interrupts that slot's voice.
    void on_data(const Event& e) noexcept;
    void on_group(const VoiceGroup& g) noexcept;
    // Call every few ms with the decoder clock: expires calls that stopped producing superframes.
    void on_time(std::uint64_t now_dibit) noexcept;
    [[nodiscard]] Privacy privacy(unsigned slot) const noexcept { return slot == 1 || slot == 2 ? privacy_[slot] : Privacy::unknown; }
    [[nodiscard]] bool active(unsigned slot) const noexcept { return (slot == 1 || slot == 2) && calls_[slot].live; }
    [[nodiscard]] const CallStats& stats() const noexcept { return stats_; }
    static constexpr std::uint64_t kSuperframeDibits = 1728;     // 6 bursts x 288 dibits
    static constexpr std::uint64_t kBurstPairDibits = 288;
    static constexpr std::uint64_t kAnchorMaxDibits = 9600;      // identity must be re-confirmed within ~2 s
    static constexpr unsigned kMaxLostSuperframes = 2;           // longer gaps end the call
private:
    struct Data {
        std::uint64_t t = 0;
        std::uint8_t slot = 0, colour_code = 0, type = 0xff;
        bool identity = false;
        std::uint32_t src = 0, dst = 0;
        std::uint8_t flco = 0, service = 0;
        bool protect_flag = false;
    };
    struct Call {
        bool live = false;
        std::uint64_t t = 0, anchor = 0, epoch = 0;
        std::uint8_t colour_code = 0;
        std::uint32_t src = 0, dst = 0;
        std::uint8_t flco = 0;
    };
    std::array<Data, 8> data_{};
    unsigned data_head_ = 0, data_count_ = 0;
    std::array<Call, 3> calls_{};
    std::array<Privacy, 3> privacy_{};
    CallStats stats_{};
    GroupSink gs_ = nullptr;
    NoticeSink ns_ = nullptr;
    void* user_ = nullptr;
    void end_call(unsigned slot, std::uint64_t at) noexcept;
    void notice(NoticeKind kind, unsigned slot, std::uint64_t at) noexcept;
};

} // namespace orcsdr::dmr::d1
