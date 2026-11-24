// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace orcsdr::dmr::trunk {
using Bytes=std::array<uint8_t,12>;
struct Block {
    Bytes bytes{};
    uint32_t epoch=0, frequency=0, ms=0;
    uint64_t dibit=0;
    uint8_t slot=0, colour=0, type=0;
    bool bs=false, fec=false, exact=false, replay=false;
};
struct Grant {
    uint32_t epoch=0, control_hz=0, ms=0, target=0, source=0, rx_hz=0, tx_hz=0;
    uint16_t lpcn=0, absolute_lpcn=0;
    uint8_t control_slot=0, control_cc=0, slot=0, cc=0;
    bool absolute=false, late=false, emergency=false, offset=false, replay=false;
};
enum class Parse : uint8_t { ignored, rejected, pending, grant };
uint16_t crc(const Bytes&) noexcept;
class Parser {
    struct Pending { Block block{}; bool valid=false; };
    std::array<Pending,2> p_{};
public:
    void reset() noexcept { p_={}; }
    // Feed every BS burst event, including unqualified blocks (fec=false).
    // No raw IQ, no allocation. Exact sync + independently qualified CACH slot required.
    Parse feed(const Block&, Grant&) noexcept;
};
struct Mapping { uint16_t lpcn=0; uint32_t hz=0; uint8_t cc=0; bool conflict=false; };
enum class MapResult:uint8_t { inserted, confirmed, conflict, full, invalid };
class ChannelMap {
    std::array<Mapping,64> m_{};
public:
    void reset() noexcept {m_={};}
    // One explicitly selected site only. No frequency guessing; conflicting mappings quarantine.
    MapResult observe(uint16_t lpcn,uint32_t hz,uint8_t cc) noexcept;
    const Mapping* find(uint16_t lpcn) const noexcept;
};
enum class State:uint8_t { idle, control, tune_voice, acquire, listening, tune_control, failed };
enum class ActionKind:uint8_t { none, tune, select_slot, mute, stop };
struct Action { ActionKind kind=ActionKind::none; uint32_t hz=0, token=0; uint8_t slot=0; bool replay=false; };
struct Config { uint32_t acquire_ms=1800, hang_ms=900, max_call_ms=180000, tune_ms=2000, duplicate_ms=2000, grant_age_ms=500; };
struct Voice {
    uint32_t epoch=0,hz=0,ms=0,target=0,source=0;
    uint8_t slot=0,cc=0;
    bool validated=false, clear=false, ended=false,replay=false;
};
class Follower {
    Config cfg_;
    State state_=State::idle;
    uint32_t control_=0,epoch_=0,started_=0,last_=0,token_=0,deadline_start_=0;
    Grant active_{},previous_{};
    uint32_t previous_ms_=0, target_filter_=0;
    bool replay_=false,previous_valid_=false;
    Action pending_{};
    ChannelMap map_;
    void go_control(uint32_t) noexcept;
    void queue(ActionKind,uint32_t,uint8_t=0) noexcept;
public:
    explicit Follower(Config c={}) noexcept:cfg_(c){}
    bool start(uint32_t control_hz,uint32_t epoch,uint32_t now,bool replay=false) noexcept;
    void stop() noexcept;
    void skip(uint32_t now) noexcept;
    // Caller serializes calls on application owner. Never call radio from here.
    void tick(uint32_t now,bool receiver_ready) noexcept;
    void discontinuity(uint32_t epoch,uint32_t now) noexcept;
    void set_target(uint32_t tg) noexcept {target_filter_=tg;}
    MapResult seed(uint16_t lpcn,uint32_t hz,uint8_t cc) noexcept {return map_.observe(lpcn,hz,cc);}
    bool grant(const Grant&,uint32_t now) noexcept;
    bool voice(const Voice&,uint32_t now) noexcept;
    Action take() noexcept {auto a=pending_;pending_={};return a;}
    // Acknowledge only current tune token after actual owner completion and epoch bump.
    bool tune_result(uint32_t token,bool accepted,uint32_t actual_hz,uint32_t epoch,uint32_t now) noexcept;
    State state()const noexcept{return state_;}
    const Grant& active()const noexcept{return active_;}
    const ChannelMap& map()const noexcept{return map_;}
    bool audio_allowed()const noexcept{return state_==State::listening;}
};
}
